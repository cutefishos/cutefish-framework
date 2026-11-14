// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#include <gtest/gtest.h>

#include <chrono>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

#include "base/event_loop.h"
#include "cutefish/packagekit/updates.h"

using namespace cutefish;
using namespace cutefish::base;
using namespace cutefish::base::dbus;
using namespace cutefish::packagekit;
using namespace std::chrono_literals;

namespace {

constexpr const char* kService = "org.freedesktop.PackageKit";
constexpr const char* kPath = "/org/freedesktop/PackageKit";
constexpr const char* kTransactionInterface = "org.freedesktop.PackageKit.Transaction";

using PackageEntry = std::tuple<uint32_t, std::string, std::string>;

// PackageKit on a connection of its own on the test's loop, so every call
// crosses the bus: the daemon, its offline interface and the transactions
// it creates.
class FakePackageKit
{
public:
    explicit FakePackageKit(EventLoop& loop)
        : m_bus(Connection::open(Connection::Bus::Session, &loop))
    {
        m_daemon = std::make_unique<Object>(m_bus.get(), kPath, kService);
        m_daemon->addMethod("CreateTransaction", [this] { return createTransaction(); });
        EXPECT_TRUE(m_daemon->publish());

        m_offline = std::make_unique<Object>(m_bus.get(), kPath, "org.freedesktop.PackageKit.Offline");
        m_offline->addMethod("GetPrepared", [this] { return prepared; });
        m_offline->addMethod("Trigger", [this](const std::string& action) { triggered.push_back(action); });
        m_offline->addMethod("ClearResults", [this] {
            ++clears;
            result.reset();
        });
        m_offline->addMethod("GetResults", "", "basutus", [this](MethodCall& call) {
            if (!result) {
                call.replyError("org.freedesktop.PackageKit.Offline.Error.NoData", "no update results available");
                return;
            }
            call.reply(result->success, result->packageIds, uint32_t(22), result->timeFinished,
                       uint32_t(result->success ? 0 : 4), result->error);
        });
        EXPECT_TRUE(m_offline->publish());
    }

    bool registerService() { return m_bus->requestName(kService); }
    void unregisterService() { m_bus->releaseName(kService); }

    // The download held by holdDownload.
    void downloadProgress(uint32_t percentage)
    {
        m_percentage = percentage;
        m_held->emitPropertiesChanged({"Percentage"});
    }
    void finishDownload()
    {
        prepared = m_heldIds;
        m_held->emitSignal("Finished", uint32_t(1), uint32_t(100));
    }

    std::vector<PackageEntry> updates;
    std::vector<std::string> prepared;
    // ErrorCode's details for a refresh that fails.
    std::optional<std::string> refreshError;
    // Refuses UpdatePackages, as polkit does for a caller it does not let through.
    bool refuseDownload = false;
    // Leaves a download running until finishDownload().
    bool holdDownload = false;
    std::optional<Updates::OfflineResult> result;

    // Every transaction method called, with its arguments.
    std::vector<std::string> calls;
    std::vector<std::vector<std::string>> hints;
    std::vector<std::string> triggered;
    int clears = 0;
    int cancels = 0;

private:
    ObjectPath createTransaction()
    {
        const std::string path = "/" + std::to_string(++m_transactions) + "_test";
        auto object = std::make_unique<Object>(m_bus.get(), path, kTransactionInterface);
        Object* transaction = object.get();
        object->addProperty("Percentage", [this] { return m_percentage; });
        object->addSignal("Packages", "a(uss)");
        object->addSignal("ErrorCode", "us");
        object->addSignal("Finished", "uu");
        object->addMethod("SetHints", [this](const std::vector<std::string>& list) { hints.push_back(list); });
        object->addMethod("Cancel", [this, transaction] {
            ++cancels;
            if (transaction != m_held)
                return;
            m_held = nullptr;
            transaction->emitSignal("ErrorCode", uint32_t(17), std::string("The task was stopped successfully"));
            transaction->emitSignal("Finished", uint32_t(3), uint32_t(100));
        });
        object->addMethod("RefreshCache", [this, transaction](bool force) {
            calls.push_back(std::string("RefreshCache ") + (force ? "true" : "false"));
            if (refreshError) {
                transaction->emitSignal("ErrorCode", uint32_t(2), *refreshError);
                transaction->emitSignal("Finished", uint32_t(2), uint32_t(10));
                return;
            }
            transaction->emitSignal("Finished", uint32_t(1), uint32_t(10));
        });
        object->addMethod("GetUpdates", [this, transaction](uint64_t filter) {
            calls.push_back("GetUpdates " + std::to_string(filter));
            transaction->emitSignal("Packages", updates);
            transaction->emitSignal("Finished", uint32_t(1), uint32_t(10));
        });
        object->addMethod("UpdatePackages", "tas", "", [this, transaction](MethodCall& call) {
            uint64_t flags = 0;
            std::vector<std::string> ids;
            call.read(flags, ids);
            std::string entry = "UpdatePackages " + std::to_string(flags);
            for (const std::string& id : ids)
                entry += " " + id;
            calls.push_back(entry);
            if (refuseDownload) {
                call.replyError("org.freedesktop.PackageKit.Transaction.RefusedByPolicy", "Not authorized");
                return;
            }
            call.reply();
            m_held = transaction;
            m_heldIds = ids;
            if (!holdDownload)
                finishDownload();
        });
        EXPECT_TRUE(object->publish());
        m_transactionObjects[path] = std::move(object);
        return {path};
    }

    std::unique_ptr<Connection> m_bus;
    std::unique_ptr<Object> m_daemon;
    std::unique_ptr<Object> m_offline;
    std::map<std::string, std::unique_ptr<Object>> m_transactionObjects;
    int m_transactions = 0;
    uint32_t m_percentage = 101;
    Object* m_held = nullptr;
    std::vector<std::string> m_heldIds;
};

class UpdatesTest : public testing::Test
{
protected:
    void SetUp() override
    {
        if (!Connection::session(&m_loop))
            GTEST_SKIP() << "No session bus; run the tests in dbus-run-session";
        m_fake = std::make_unique<FakePackageKit>(m_loop);
        ASSERT_TRUE(m_fake->registerService());
        m_fake->updates = {
            {5, "tzdata;2026f-0+deb13u1;all;debian-stable-updates", "time zone and daylight-saving time data"},
            {9, "linux-image-amd64;6.12.99-1;amd64;debian-stable", "Linux for 64-bit PCs"},
            {(6 << 16) | 8, "openssl;3.5.4-1~deb13u2;amd64;debian-security", "Secure Sockets Layer toolkit"},
        };
    }

    bool waitFor(const std::function<bool()>& condition)
    {
        const auto deadline = std::chrono::steady_clock::now() + 2s;
        while (!condition()) {
            if (std::chrono::steady_clock::now() > deadline)
                return false;
            runFor(5ms);
        }
        return true;
    }

    void runFor(std::chrono::milliseconds time)
    {
        const int timer = m_loop.startTimer(time, false, [this] { m_loop.quit(); });
        m_loop.run();
        m_loop.stopTimer(timer);
    }

    bool settlesIn(Updates& updates, Updates::State state)
    {
        return waitFor([&] { return updates.state() == state; });
    }

    Connection* bus() { return Connection::session(&m_loop); }

    EventLoop m_loop;
    std::unique_ptr<FakePackageKit> m_fake;
};

} // namespace

TEST_F(UpdatesTest, LoadsWithoutFetchingTheLists)
{
    Updates updates(bus());
    EXPECT_EQ(updates.state(), Updates::State::Unknown);
    updates.load();
    EXPECT_EQ(updates.state(), Updates::State::Checking);
    ASSERT_TRUE(settlesIn(updates, Updates::State::Available));
    EXPECT_EQ(m_fake->calls, (std::vector<std::string>{"GetUpdates 2"}));

    // Held-back packages are left out, the rest sorted by name.
    ASSERT_EQ(updates.packages().size(), 2u);
    EXPECT_EQ(updates.packages()[0].name(), "openssl");
    EXPECT_EQ(updates.packages()[0].version(), "3.5.4-1~deb13u2");
    EXPECT_EQ(updates.packages()[0].summary, "Secure Sockets Layer toolkit");
    EXPECT_EQ(updates.packages()[1].name(), "tzdata");
    EXPECT_EQ(updates.packages()[1].version(), "2026f-0+deb13u1");
}

TEST_F(UpdatesTest, ChecksAfterFetchingTheLists)
{
    Updates updates(bus());
    updates.check(false);
    ASSERT_TRUE(settlesIn(updates, Updates::State::Available));
    EXPECT_EQ(m_fake->calls, (std::vector<std::string>{"RefreshCache false", "GetUpdates 2"}));
    ASSERT_EQ(m_fake->hints.size(), 2u);
    EXPECT_EQ(m_fake->hints[0], (std::vector<std::string>{"interactive=false", "background=true"}));

    m_fake->calls.clear();
    m_fake->hints.clear();
    m_fake->updates.clear();
    updates.check(true);
    ASSERT_TRUE(settlesIn(updates, Updates::State::UpToDate));
    EXPECT_TRUE(updates.packages().empty());
    ASSERT_EQ(m_fake->hints.size(), 2u);
    EXPECT_EQ(m_fake->hints[0], (std::vector<std::string>{"interactive=true"}));
}

TEST_F(UpdatesTest, AFailedRefreshIsAnError)
{
    m_fake->refreshError = "Cannot download the package lists";
    Updates updates(bus());
    updates.check(true);
    ASSERT_TRUE(settlesIn(updates, Updates::State::Error));
    EXPECT_EQ(updates.error(), "Cannot download the package lists");
    EXPECT_EQ(m_fake->calls, (std::vector<std::string>{"RefreshCache false"}));

    // A later check starts over.
    m_fake->refreshError.reset();
    updates.check(true);
    ASSERT_TRUE(settlesIn(updates, Updates::State::Available));
    EXPECT_EQ(updates.error(), "");
}

TEST_F(UpdatesTest, DownloadsForTheNextBoot)
{
    m_fake->holdDownload = true;
    Updates updates(bus());
    updates.load();
    ASSERT_TRUE(settlesIn(updates, Updates::State::Available));

    updates.download(true);
    EXPECT_EQ(updates.state(), Updates::State::Downloading);
    EXPECT_EQ(updates.progress(), -1);
    ASSERT_TRUE(waitFor([&] { return m_fake->calls.size() == 2; }));
    // Only trusted packages, only downloaded.
    EXPECT_EQ(m_fake->calls[1], "UpdatePackages 10 openssl;3.5.4-1~deb13u2;amd64;debian-security "
                                "tzdata;2026f-0+deb13u1;all;debian-stable-updates");

    m_fake->downloadProgress(42);
    ASSERT_TRUE(waitFor([&] { return updates.progress() == 42; }));
    m_fake->downloadProgress(101);
    ASSERT_TRUE(waitFor([&] { return updates.progress() == -1; }));
    EXPECT_EQ(updates.state(), Updates::State::Downloading);

    m_fake->finishDownload();
    ASSERT_TRUE(settlesIn(updates, Updates::State::Ready));
    EXPECT_EQ(updates.packages().size(), 2u);
}

TEST_F(UpdatesTest, AStoppedDownloadIsAvailableAgain)
{
    m_fake->holdDownload = true;
    Updates updates(bus());
    updates.load();
    ASSERT_TRUE(settlesIn(updates, Updates::State::Available));
    updates.download(true);
    ASSERT_TRUE(waitFor([&] { return m_fake->calls.size() == 2; }));
    m_fake->downloadProgress(42);
    ASSERT_TRUE(waitFor([&] { return updates.progress() == 42; }));

    std::vector<Updates::State> states;
    Subscription changed = updates.changed.connect([&] { states.push_back(updates.state()); });
    updates.cancelDownload();
    ASSERT_TRUE(settlesIn(updates, Updates::State::Available));
    EXPECT_EQ(m_fake->cancels, 1);
    EXPECT_EQ(states, std::vector<Updates::State>{Updates::State::Available});
    EXPECT_EQ(updates.error(), "");
    EXPECT_EQ(updates.packages().size(), 2u);
    EXPECT_TRUE(m_fake->prepared.empty());

    // Stopped before PackageKit had it, nothing runs.
    m_fake->calls.clear();
    updates.download(true);
    updates.cancelDownload();
    ASSERT_TRUE(settlesIn(updates, Updates::State::Available));
    runFor(50ms);
    EXPECT_TRUE(m_fake->calls.empty());
    EXPECT_EQ(m_fake->cancels, 1);
}

TEST_F(UpdatesTest, ARefusedDownloadIsAnError)
{
    m_fake->refuseDownload = true;
    Updates updates(bus());
    updates.load();
    ASSERT_TRUE(settlesIn(updates, Updates::State::Available));
    updates.download(true);
    ASSERT_TRUE(settlesIn(updates, Updates::State::Error));
    EXPECT_EQ(updates.error(), "Not authorized");
}

TEST_F(UpdatesTest, DownloadedUpdatesAreReady)
{
    m_fake->prepared = {"openssl;3.5.4-1~deb13u2;amd64;debian-security",
                        "tzdata;2026f-0+deb13u1;all;debian-stable-updates"};
    Updates updates(bus());
    updates.load();
    ASSERT_TRUE(settlesIn(updates, Updates::State::Ready));

    // A new update since the download needs another one.
    m_fake->updates.push_back({5, "zstd;1.5.7+dfsg-1;amd64;debian-stable", "fast lossless compression"});
    updates.check(false);
    ASSERT_TRUE(settlesIn(updates, Updates::State::Available));
    EXPECT_EQ(updates.packages().size(), 3u);

    // Downloads are only started from the list.
    m_fake->calls.clear();
    updates.download(true);
    ASSERT_TRUE(settlesIn(updates, Updates::State::Ready));
    updates.download(true);
    runFor(50ms);
    EXPECT_EQ(m_fake->calls.size(), 1u);
}

TEST_F(UpdatesTest, AFailedBackgroundCheckChangesNothing)
{
    m_fake->prepared = {"openssl;3.5.4-1~deb13u2;amd64;debian-security",
                        "tzdata;2026f-0+deb13u1;all;debian-stable-updates"};
    Updates updates(bus());
    updates.load();
    ASSERT_TRUE(settlesIn(updates, Updates::State::Ready));
    const std::vector<Package> listed = updates.packages();

    std::vector<bool> finished;
    std::vector<Updates::State> states;
    Subscription checked = updates.checkFinished.connect([&](bool success) { finished.push_back(success); });
    Subscription changed = updates.changed.connect([&] { states.push_back(updates.state()); });
    m_fake->refreshError = "Cannot download the package lists";
    m_fake->hints.clear();
    updates.checkInBackground();
    EXPECT_TRUE(updates.isCheckingInBackground());
    EXPECT_EQ(updates.state(), Updates::State::Ready);
    ASSERT_TRUE(waitFor([&] { return !finished.empty(); }));
    EXPECT_EQ(finished, std::vector<bool>{false});
    EXPECT_FALSE(updates.isCheckingInBackground());
    EXPECT_TRUE(states.empty());
    EXPECT_EQ(updates.state(), Updates::State::Ready);
    EXPECT_EQ(updates.error(), "");
    ASSERT_EQ(updates.packages().size(), listed.size());
    EXPECT_EQ(updates.packages()[0].id, listed[0].id);
    EXPECT_EQ(m_fake->hints[0], (std::vector<std::string>{"interactive=false", "background=true"}));

    // One that goes through shows its result without passing through Checking.
    m_fake->refreshError.reset();
    m_fake->updates.push_back({5, "zstd;1.5.7+dfsg-1;amd64;debian-stable", "fast lossless compression"});
    updates.checkInBackground();
    ASSERT_TRUE(waitFor([&] { return finished.size() == 2; }));
    EXPECT_TRUE(finished[1]);
    EXPECT_EQ(states, std::vector<Updates::State>{Updates::State::Available});
    EXPECT_EQ(updates.packages().size(), 3u);
}

TEST_F(UpdatesTest, ACheckTakesOverFromABackgroundOne)
{
    m_fake->refreshError = "Cannot download the package lists";
    Updates updates(bus());
    updates.load();
    ASSERT_TRUE(settlesIn(updates, Updates::State::Available));
    updates.checkInBackground();
    updates.check(true);
    EXPECT_FALSE(updates.isCheckingInBackground());
    // A check from the user shows its failure.
    ASSERT_TRUE(settlesIn(updates, Updates::State::Error));
    EXPECT_EQ(updates.error(), "Cannot download the package lists");
}

TEST_F(UpdatesTest, TriggersARebootInstall)
{
    Updates updates(bus());
    std::optional<bool> succeeded;
    PendingCall call = updates.trigger([&](bool success, const std::string&) { succeeded = success; });
    ASSERT_TRUE(waitFor([&] { return succeeded.has_value(); }));
    EXPECT_TRUE(*succeeded);
    EXPECT_EQ(m_fake->triggered, (std::vector<std::string>{"reboot"}));
}

TEST_F(UpdatesTest, ReadsTheOfflineResult)
{
    Updates updates(bus());
    std::optional<std::optional<Updates::OfflineResult>> read;
    const auto readResult = [&] {
        read.reset();
        PendingCall call = updates.readOfflineResult([&](const std::optional<Updates::OfflineResult>& result) {
            read = result;
        });
        return waitFor([&] { return read.has_value(); });
    };

    ASSERT_TRUE(readResult());
    EXPECT_FALSE(read->has_value());

    m_fake->result = Updates::OfflineResult{true, {"tzdata;2026f-0+deb13u1;all;installed"}, 1794000000, {}};
    ASSERT_TRUE(readResult());
    ASSERT_TRUE(read->has_value());
    EXPECT_TRUE((*read)->success);
    EXPECT_EQ((*read)->packageIds, (std::vector<std::string>{"tzdata;2026f-0+deb13u1;all;installed"}));
    EXPECT_EQ((*read)->timeFinished, 1794000000u);
    EXPECT_EQ((*read)->error, "");

    m_fake->result = Updates::OfflineResult{false, {}, 1794000000, "dpkg returned an error code (1)"};
    ASSERT_TRUE(readResult());
    ASSERT_TRUE(read->has_value());
    EXPECT_FALSE((*read)->success);
    EXPECT_EQ((*read)->error, "dpkg returned an error code (1)");

    updates.clearOfflineResult();
    ASSERT_TRUE(waitFor([&] { return m_fake->clears == 1; }));
    ASSERT_TRUE(readResult());
    EXPECT_FALSE(read->has_value());
}

TEST_F(UpdatesTest, AvailabilityFollowsTheDaemon)
{
    std::optional<bool> available;
    {
        Availability check(bus(), [&](bool answer) { available = answer; });
        ASSERT_TRUE(waitFor([&] { return available.has_value(); }));
        EXPECT_TRUE(*available);
    }

    // A session bus has no PackageKit to start either.
    m_fake->unregisterService();
    available.reset();
    Availability check(bus(), [&](bool answer) { available = answer; });
    ASSERT_TRUE(waitFor([&] { return available.has_value(); }));
    EXPECT_FALSE(*available);
}
