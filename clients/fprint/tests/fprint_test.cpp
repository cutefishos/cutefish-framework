// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#include <gtest/gtest.h>
#include <systemd/sd-bus.h>

#include <atomic>
#include <chrono>
#include <future>
#include <mutex>
#include <thread>

#include "base/event_loop.h"
#include "cutefish/fprint/fingerprint_reader.h"

using namespace cutefish;
using namespace cutefish::base;
using namespace cutefish::base::dbus;
using namespace std::chrono_literals;

namespace {

constexpr const char* kDevicePath = "/net/reactivated/Fprint/Device/0";

int getAllProperties(sd_bus_message* message, void*, sd_bus_error*)
{
    if (!sd_bus_message_is_method_call(message, "org.freedesktop.DBus.Properties", "GetAll"))
        return 0;
    sd_bus_message* reply = nullptr;
    sd_bus_message_new_method_return(message, &reply);
    sd_bus_message_open_container(reply, 'a', "{sv}");
    sd_bus_message_append(reply, "{sv}", "name", "s", "Test Sensor");
    sd_bus_message_append(reply, "{sv}", "scan-type", "s", "press");
    sd_bus_message_append(reply, "{sv}", "num-enroll-stages", "i", int32_t(21));
    sd_bus_message_close_container(reply);
    sd_bus_send(nullptr, reply, nullptr);
    sd_bus_message_unref(reply);
    return 1;
}

// net.reactivated.Fprint on the session bus, on a thread of its own, with
// the one reader the way fprintd shows it.
class FakeFprintd
{
public:
    struct Options
    {
        bool hasDevice = true;
        bool denyEnroll = false;
        bool busy = false;
        std::vector<std::string> enrolled = {"right-index-finger"};
    };

    explicit FakeFprintd(Options options)
        : m_options(std::move(options))
        , m_enrolled(m_options.enrolled)
    {
        std::promise<bool> ready;
        m_thread = std::thread([this, &ready] { run(ready); });
        m_ready = ready.get_future().get();
    }
    ~FakeFprintd()
    {
        m_handle.post([this] { m_loop->quit(); });
        m_thread.join();
    }

    bool isReady() const { return m_ready; }
    int claims() const { return m_claims; }
    int releases() const { return m_releases; }
    int enrollStops() const { return m_enrollStops; }
    bool askedInteractively() const { return m_interactive; }
    std::string enrolling() const
    {
        std::lock_guard guard(m_mutex);
        return m_enrolling;
    }
    std::vector<std::string> enrolled() const
    {
        std::lock_guard guard(m_mutex);
        return m_enrolled;
    }

    // What the driver reports while a finger is being recorded.
    void status(const std::string& result, bool done)
    {
        m_handle.post([this, result, done] {
            if (done && result == "enroll-completed") {
                std::lock_guard guard(m_mutex);
                m_enrolled.push_back(m_enrolling);
            }
            m_device->emitSignal("EnrollStatus", result, done);
        });
    }

private:
    void run(std::promise<bool>& ready)
    {
        EventLoop loop;
        m_loop = &loop;
        m_handle = loop.handle();
        std::unique_ptr<Connection> connection = Connection::open(Connection::Bus::Session, &loop);
        if (!connection || !connection->requestName("net.reactivated.Fprint")) {
            ready.set_value(false);
            return;
        }
        Object manager(connection.get(), "/net/reactivated/Fprint/Manager", "net.reactivated.Fprint.Manager");
        manager.addMethod("GetDefaultDevice", "", "o", [this](MethodCall& call) {
            if (!m_options.hasDevice) {
                call.replyError("net.reactivated.Fprint.Error.NoSuchDevice", "No devices available");
                return;
            }
            call.reply(ObjectPath{kDevicePath});
        });
        Object device(connection.get(), kDevicePath, "net.reactivated.Fprint.Device");
        m_device = &device;
        device.addMethod("Claim", "s", "", [this](MethodCall& call) {
            m_interactive = call.message().allowsInteractiveAuthorization();
            if (m_options.busy) {
                call.replyError("net.reactivated.Fprint.Error.AlreadyInUse", "Device was already claimed");
                return;
            }
            ++m_claims;
            call.reply();
        });
        device.addMethod("Release", "", "", [this](MethodCall& call) {
            ++m_releases;
            call.reply();
        });
        device.addMethod("ListEnrolledFingers", "s", "as", [this](MethodCall& call) {
            std::lock_guard guard(m_mutex);
            if (m_enrolled.empty()) {
                call.replyError("net.reactivated.Fprint.Error.NoEnrolledPrints", "No fingerprints enrolled");
                return;
            }
            call.reply(m_enrolled);
        });
        device.addMethod("DeleteEnrolledFinger", "s", "", [this](MethodCall& call) {
            std::string finger;
            call.read(finger);
            std::lock_guard guard(m_mutex);
            std::erase(m_enrolled, finger);
            call.reply();
        });
        device.addMethod("EnrollStart", "s", "", [this](MethodCall& call) {
            if (m_options.denyEnroll) {
                call.replyError("net.reactivated.Fprint.Error.PermissionDenied",
                                "Not Authorized: net.reactivated.fprint.device.enroll");
                return;
            }
            std::lock_guard guard(m_mutex);
            call.read(m_enrolling);
            call.reply();
        });
        device.addMethod("EnrollStop", "", "", [this](MethodCall& call) {
            ++m_enrollStops;
            call.reply();
        });
        device.addSignal("EnrollStatus", "sb");
        // The property names have hyphens, which the toolkit's exported
        // properties (and sd-bus) refuse, so GetAll is answered by hand.
        sd_bus_slot* properties = nullptr;
        sd_bus_add_object(connection->bus(), &properties, kDevicePath, getAllProperties, nullptr);
        ready.set_value(manager.publish() && device.publish() && properties);
        loop.run();
        m_device = nullptr;
        sd_bus_slot_unref(properties);
    }

    Options m_options;
    std::thread m_thread;
    EventLoop* m_loop = nullptr;
    LoopHandle m_handle;
    Object* m_device = nullptr;
    mutable std::mutex m_mutex;
    std::vector<std::string> m_enrolled;
    std::string m_enrolling;
    std::atomic<int> m_claims = 0;
    std::atomic<int> m_releases = 0;
    std::atomic<int> m_enrollStops = 0;
    std::atomic<bool> m_interactive = false;
    bool m_ready = false;
};

class FprintTest : public testing::Test
{
protected:
    void SetUp() override
    {
        if (!Connection::session(&m_loop))
            GTEST_SKIP() << "No session bus; run the tests in dbus-run-session";
    }

    void start(FakeFprintd::Options options = {})
    {
        m_service = std::make_unique<FakeFprintd>(std::move(options));
        ASSERT_TRUE(m_service->isReady());
    }

    bool waitFor(const std::function<bool()>& condition)
    {
        const auto deadline = std::chrono::steady_clock::now() + 2s;
        while (!condition()) {
            if (std::chrono::steady_clock::now() > deadline)
                return false;
            const int timer = m_loop.startTimer(5ms, false, [this] { m_loop.quit(); });
            m_loop.run();
            m_loop.stopTimer(timer);
        }
        return true;
    }

    EventLoop m_loop;
    std::unique_ptr<FakeFprintd> m_service;
};

} // namespace

TEST_F(FprintTest, ReadsTheReaderAndTheStoredFingers)
{
    start({.enrolled = {"right-index-finger", "left-thumb", "no-such-finger"}});
    fprint::Reader reader(Connection::session(&m_loop));
    ASSERT_TRUE(waitFor([&] { return reader.isReady() && reader.isListed(); }));
    EXPECT_TRUE(reader.isAvailable());
    EXPECT_EQ(reader.name(), "Test Sensor");
    EXPECT_EQ(reader.scanType(), fprint::ScanType::Press);
    EXPECT_EQ(reader.enrollStages(), 21);
    EXPECT_EQ(reader.enrolled(), (std::vector<fprint::Finger>{fprint::Finger::LeftThumb, fprint::Finger::RightIndex}));
    EXPECT_EQ(reader.listError(), "");
}

TEST_F(FprintTest, NoReaderMeansNotAvailable)
{
    start({.hasDevice = false});
    fprint::Reader reader(Connection::session(&m_loop));
    ASSERT_TRUE(waitFor([&] { return reader.isReady(); }));
    EXPECT_FALSE(reader.isAvailable());
    EXPECT_TRUE(reader.enrolled().empty());
    EXPECT_EQ(reader.enroll(fprint::Finger::RightIndex), nullptr);
}

TEST_F(FprintTest, NoReaderWithoutTheService)
{
    fprint::Reader reader(Connection::session(&m_loop));
    ASSERT_TRUE(waitFor([&] { return reader.isReady(); }));
    EXPECT_FALSE(reader.isAvailable());
}

TEST_F(FprintTest, NoStoredFingersIsNotAnError)
{
    start({.enrolled = {}});
    fprint::Reader reader(Connection::session(&m_loop));
    ASSERT_TRUE(waitFor([&] { return reader.isListed(); }));
    EXPECT_TRUE(reader.enrolled().empty());
    EXPECT_EQ(reader.listError(), "");
}

TEST_F(FprintTest, DeletingClaimsTheReaderAndGivesItBack)
{
    start({.enrolled = {"right-index-finger", "left-thumb"}});
    fprint::Reader reader(Connection::session(&m_loop));
    ASSERT_TRUE(waitFor([&] { return reader.isListed(); }));

    bool done = false;
    fprint::Failure failure = fprint::Failure::Failed;
    reader.remove(fprint::Finger::LeftThumb, [&](fprint::Failure result, const std::string&) {
        failure = result;
        done = true;
    });
    ASSERT_TRUE(waitFor([&] { return done; }));
    EXPECT_EQ(failure, fprint::Failure::None);
    EXPECT_TRUE(m_service->askedInteractively());
    ASSERT_TRUE(waitFor([&] { return m_service->releases() == 1; }));
    EXPECT_EQ(m_service->claims(), 1);
    EXPECT_EQ(m_service->enrolled(), (std::vector<std::string>{"right-index-finger"}));
    ASSERT_TRUE(waitFor([&] { return reader.enrolled().size() == 1; }));
    EXPECT_EQ(reader.enrolled().front(), fprint::Finger::RightIndex);
}

TEST_F(FprintTest, DeletingWhileTheReaderIsBusyFails)
{
    start({.busy = true});
    fprint::Reader reader(Connection::session(&m_loop));
    ASSERT_TRUE(waitFor([&] { return reader.isListed(); }));
    fprint::Failure failure = fprint::Failure::None;
    bool done = false;
    reader.remove(fprint::Finger::RightIndex, [&](fprint::Failure result, const std::string&) {
        failure = result;
        done = true;
    });
    ASSERT_TRUE(waitFor([&] { return done; }));
    EXPECT_EQ(failure, fprint::Failure::InUse);
    EXPECT_EQ(m_service->enrolled().size(), 1u);
}

TEST_F(FprintTest, EnrollmentFollowsTheScans)
{
    start({.enrolled = {}});
    fprint::Reader reader(Connection::session(&m_loop));
    ASSERT_TRUE(waitFor([&] { return reader.isListed(); }));

    std::unique_ptr<fprint::Enrollment> enrollment = reader.enroll(fprint::Finger::LeftIndex);
    ASSERT_NE(enrollment, nullptr);
    EXPECT_EQ(enrollment->state(), fprint::Enrollment::State::Starting);
    EXPECT_EQ(enrollment->stages(), 21);
    ASSERT_TRUE(waitFor([&] { return enrollment->state() == fprint::Enrollment::State::Scanning; }));
    EXPECT_TRUE(m_service->askedInteractively());
    EXPECT_EQ(m_service->enrolling(), "left-index-finger");

    m_service->status("enroll-stage-passed", false);
    m_service->status("enroll-stage-passed", false);
    ASSERT_TRUE(waitFor([&] { return enrollment->stagesPassed() == 2; }));
    m_service->status("enroll-retry-scan", false);
    ASSERT_TRUE(waitFor([&] { return enrollment->hint() == fprint::Enrollment::Hint::RetryScan; }));
    EXPECT_EQ(enrollment->stagesPassed(), 2);
    m_service->status("enroll-stage-passed", false);
    ASSERT_TRUE(waitFor([&] { return enrollment->stagesPassed() == 3; }));
    EXPECT_EQ(enrollment->hint(), fprint::Enrollment::Hint::None);

    m_service->status("enroll-completed", true);
    ASSERT_TRUE(waitFor([&] { return enrollment->state() == fprint::Enrollment::State::Completed; }));
    EXPECT_EQ(enrollment->stagesPassed(), 21);
    // The reader is stopped, then given back, and the list follows.
    ASSERT_TRUE(waitFor([&] { return m_service->releases() == 1; }));
    EXPECT_EQ(m_service->enrollStops(), 1);
    ASSERT_TRUE(waitFor([&] { return reader.enrolled().size() == 1; }));
    EXPECT_EQ(reader.enrolled().front(), fprint::Finger::LeftIndex);
}

TEST_F(FprintTest, EnrollmentRefusedByPolkitGivesTheReaderBack)
{
    start({.denyEnroll = true, .enrolled = {}});
    fprint::Reader reader(Connection::session(&m_loop));
    ASSERT_TRUE(waitFor([&] { return reader.isListed(); }));
    std::unique_ptr<fprint::Enrollment> enrollment = reader.enroll(fprint::Finger::RightIndex);
    ASSERT_TRUE(waitFor([&] { return enrollment->state() == fprint::Enrollment::State::Failed; }));
    EXPECT_EQ(enrollment->failure(), fprint::Failure::NotAuthorized);
    ASSERT_TRUE(waitFor([&] { return m_service->releases() == 1; }));
    EXPECT_EQ(m_service->enrollStops(), 0);
}

TEST_F(FprintTest, EnrollmentOnABusyReaderFails)
{
    start({.busy = true, .enrolled = {}});
    fprint::Reader reader(Connection::session(&m_loop));
    ASSERT_TRUE(waitFor([&] { return reader.isListed(); }));
    std::unique_ptr<fprint::Enrollment> enrollment = reader.enroll(fprint::Finger::RightIndex);
    ASSERT_TRUE(waitFor([&] { return enrollment->state() == fprint::Enrollment::State::Failed; }));
    EXPECT_EQ(enrollment->failure(), fprint::Failure::InUse);
    // Nothing was claimed, so nothing is released.
    EXPECT_EQ(m_service->releases(), 0);
}

TEST_F(FprintTest, EnrollmentReportsWhyItFailed)
{
    start({.enrolled = {}});
    fprint::Reader reader(Connection::session(&m_loop));
    ASSERT_TRUE(waitFor([&] { return reader.isListed(); }));
    std::unique_ptr<fprint::Enrollment> enrollment = reader.enroll(fprint::Finger::RightIndex);
    ASSERT_TRUE(waitFor([&] { return enrollment->state() == fprint::Enrollment::State::Scanning; }));
    m_service->status("enroll-duplicate", true);
    ASSERT_TRUE(waitFor([&] { return enrollment->state() == fprint::Enrollment::State::Failed; }));
    EXPECT_EQ(enrollment->failure(), fprint::Failure::Duplicate);
    ASSERT_TRUE(waitFor([&] { return m_service->releases() == 1; }));
    EXPECT_TRUE(reader.enrolled().empty());
}

TEST_F(FprintTest, CancellingStopsTheEnrollmentAndGivesTheReaderBack)
{
    start({.enrolled = {}});
    fprint::Reader reader(Connection::session(&m_loop));
    ASSERT_TRUE(waitFor([&] { return reader.isListed(); }));
    std::unique_ptr<fprint::Enrollment> enrollment = reader.enroll(fprint::Finger::RightIndex);
    ASSERT_TRUE(waitFor([&] { return enrollment->state() == fprint::Enrollment::State::Scanning; }));
    enrollment->cancel();
    EXPECT_EQ(enrollment->state(), fprint::Enrollment::State::Cancelled);
    ASSERT_TRUE(waitFor([&] { return m_service->releases() == 1; }));
    EXPECT_EQ(m_service->enrollStops(), 1);
}

TEST_F(FprintTest, DroppingAnEnrollmentGivesTheReaderBack)
{
    start({.enrolled = {}});
    fprint::Reader reader(Connection::session(&m_loop));
    ASSERT_TRUE(waitFor([&] { return reader.isListed(); }));
    {
        std::unique_ptr<fprint::Enrollment> enrollment = reader.enroll(fprint::Finger::RightIndex);
        ASSERT_TRUE(waitFor([&] { return enrollment->state() == fprint::Enrollment::State::Scanning; }));
    }
    ASSERT_TRUE(waitFor([&] { return m_service->releases() == 1; }));
    EXPECT_EQ(m_service->enrollStops(), 1);
}
