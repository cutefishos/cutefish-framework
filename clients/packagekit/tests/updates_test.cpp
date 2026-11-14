// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#include <gtest/gtest.h>

#include <algorithm>
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

// PK_STATUS_ENUM_DOWNLOAD and PK_STATUS_ENUM_INSTALL.
constexpr uint32_t kStatusDownload = 8;
constexpr uint32_t kStatusInstall = 9;

using PackageEntry = std::tuple<uint32_t, std::string, std::string>;

// PackageKit on a connection of its own on the test's loop, so every call
// crosses the bus: the daemon and the transactions it creates.
class FakePackageKit
{
public:
    explicit FakePackageKit(EventLoop& loop)
        : m_loop(loop)
        , m_bus(Connection::open(Connection::Bus::Session, &loop))
    {
        m_daemon = std::make_unique<Object>(m_bus.get(), kPath, kService);
        m_daemon->addMethod("CreateTransaction", [this] { return createTransaction(0); });
        m_daemon->addMethod("GetTransactionList", [this] {
            std::vector<ObjectPath> paths;
            for (const auto& [path, transaction] : m_transactions) {
                if (!transaction.finished)
                    paths.push_back({path});
            }
            return paths;
        });
        EXPECT_TRUE(m_daemon->publish());
    }

    bool registerService() { return m_bus->requestName(kService); }
    void unregisterService() { m_bus->releaseName(kService); }

    // An update another process started, already downloading.
    std::string startForeignUpdate()
    {
        const std::string path = createTransaction(22).path;
        m_held = path;
        set(path, kStatusDownload, 10, true);
        return path;
    }

    // An update that ends, its object gone, right after its first look.
    void startVanishingUpdate()
    {
        const std::string path = createTransaction(22).path;
        m_transactions[path].vanishes = true;
        set(path, kStatusDownload, 10, true);
    }

    // The update held by holdUpdate, or started by startForeignUpdate.
    void progress(uint32_t status, uint32_t percentage, bool allowCancel)
    {
        set(m_held, status, percentage, allowCancel);
    }
    void finishUpdate()
    {
        finish(m_held, 1);
        m_held.clear();
    }

    std::vector<PackageEntry> updates;
    // ErrorCode's details for a refresh that fails.
    std::optional<std::string> refreshError;
    // Turns the update down as polkit does when the prompt is dismissed.
    bool refuseAuthorization = false;
    // Leaves an update running until finishUpdate() or Cancel.
    bool holdUpdate = false;

    // Every transaction method called, with its arguments.
    std::vector<std::string> calls;
    std::vector<std::vector<std::string>> hints;

private:
    struct Fake
    {
        std::unique_ptr<Object> object;
        uint32_t role = 0;
        uint32_t status = 0;
        uint32_t percentage = 101;
        bool allowCancel = false;
        bool finished = false;
        bool vanishes = false;
    };

    ObjectPath createTransaction(uint32_t role)
    {
        const std::string path = "/" + std::to_string(++m_count) + "_test";
        Fake& fake = m_transactions[path];
        fake.role = role;
        fake.object = std::make_unique<Object>(m_bus.get(), path, kTransactionInterface);
        Object* object = fake.object.get();
        object->addProperty("Role", [this, path] {
            Fake& fake = m_transactions[path];
            if (fake.vanishes) {
                m_loop.startTimer(0ms, false, [this, path] {
                    Fake& gone = m_transactions[path];
                    gone.finished = true;
                    gone.object.reset();
                });
            }
            return fake.role;
        });
        object->addProperty("Status", [this, path] { return m_transactions[path].status; });
        object->addProperty("Percentage", [this, path] { return m_transactions[path].percentage; });
        object->addProperty("AllowCancel", [this, path] { return m_transactions[path].allowCancel; });
        object->addProperty("TransactionFlags", [] { return uint64_t(1) << 1; });
        object->addSignal("Packages", "a(uss)");
        object->addSignal("ErrorCode", "us");
        object->addSignal("Finished", "uu");
        object->addMethod("SetHints", [this](const std::vector<std::string>& list) { hints.push_back(list); });
        object->addMethod("Cancel", [this, path] {
            calls.push_back("Cancel");
            if (path != m_held)
                return;
            m_held.clear();
            finish(path, 3);
        });
        object->addMethod("RefreshCache", [this, path, object](bool force) {
            calls.push_back(std::string("RefreshCache ") + (force ? "true" : "false"));
            if (refreshError)
                object->emitSignal("ErrorCode", uint32_t(2), *refreshError);
            finish(path, refreshError ? 2 : 1);
        });
        object->addMethod("GetUpdates", [this, path, object](uint64_t filter) {
            calls.push_back("GetUpdates " + std::to_string(filter));
            object->emitSignal("Packages", updates);
            finish(path, 1);
        });
        object->addMethod("UpdatePackages", [this, path, object](uint64_t flags, const std::vector<std::string>& ids) {
            std::string entry = "UpdatePackages " + std::to_string(flags);
            for (const std::string& id : ids)
                entry += " " + id;
            calls.push_back(entry);
            m_transactions[path].role = 22;
            if (refuseAuthorization) {
                object->emitSignal("ErrorCode", uint32_t(48), std::string("Failed to obtain authentication."));
                finish(path, 2);
                return;
            }
            m_held = path;
            set(path, kStatusDownload, 0, true);
            if (!holdUpdate) {
                set(path, kStatusInstall, 50, false);
                finishUpdate();
            }
        });
        EXPECT_TRUE(object->publish());
        return {path};
    }

    void set(const std::string& path, uint32_t status, uint32_t percentage, bool allowCancel)
    {
        Fake& fake = m_transactions[path];
        fake.status = status;
        fake.percentage = percentage;
        fake.allowCancel = allowCancel;
        fake.object->emitPropertiesChanged({"Status", "Percentage", "AllowCancel"});
    }

    void finish(const std::string& path, uint32_t exit)
    {
        Fake& fake = m_transactions[path];
        fake.finished = true;
        fake.object->emitSignal("Finished", exit, uint32_t(10));
    }

    EventLoop& m_loop;
    std::unique_ptr<Connection> m_bus;
    std::unique_ptr<Object> m_daemon;
    std::map<std::string, Fake> m_transactions;
    int m_count = 0;
    std::string m_held;
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

TEST_F(UpdatesTest, InstallsDownloadingThenInstalling)
{
    m_fake->holdUpdate = true;
    Updates updates(bus());
    updates.load();
    ASSERT_TRUE(settlesIn(updates, Updates::State::Available));
    int installed = 0;
    Subscription subscription = updates.installed.connect([&] { ++installed; });

    updates.update(true);
    EXPECT_EQ(updates.state(), Updates::State::Updating);
    EXPECT_EQ(updates.phase(), Updates::Phase::Downloading);
    ASSERT_TRUE(waitFor([&] { return m_fake->calls.size() == 2; }));
    // Only trusted packages, installed now.
    EXPECT_EQ(m_fake->calls[1], "UpdatePackages 2 openssl;3.5.4-1~deb13u2;amd64;debian-security "
                                "tzdata;2026f-0+deb13u1;all;debian-stable-updates");
    ASSERT_TRUE(waitFor([&] { return updates.canCancel(); }));

    m_fake->progress(kStatusDownload, 42, true);
    ASSERT_TRUE(waitFor([&] { return updates.progress() == 42; }));
    EXPECT_EQ(updates.phase(), Updates::Phase::Downloading);
    EXPECT_TRUE(updates.canCancel());

    // Once it installs, it can no longer be stopped.
    m_fake->progress(kStatusInstall, 80, false);
    ASSERT_TRUE(waitFor([&] { return updates.progress() == 80; }));
    EXPECT_EQ(updates.phase(), Updates::Phase::Installing);
    EXPECT_FALSE(updates.canCancel());
    EXPECT_EQ(installed, 0);

    m_fake->updates.clear();
    m_fake->finishUpdate();
    ASSERT_TRUE(settlesIn(updates, Updates::State::UpToDate));
    EXPECT_EQ(installed, 1);
    EXPECT_EQ(m_fake->calls.back(), "GetUpdates 2");
}

TEST_F(UpdatesTest, ADismissedPasswordPromptLeavesTheUpdatesAvailable)
{
    m_fake->refuseAuthorization = true;
    Updates updates(bus());
    updates.load();
    ASSERT_TRUE(settlesIn(updates, Updates::State::Available));
    std::vector<Updates::State> states;
    Subscription subscription = updates.changed.connect([&] { states.push_back(updates.state()); });
    updates.update(true);
    ASSERT_TRUE(waitFor([&] { return states.size() >= 2; }));
    EXPECT_EQ(states.back(), Updates::State::Available);
    EXPECT_EQ(updates.error(), "");
    EXPECT_EQ(updates.packages().size(), 2u);
}

TEST_F(UpdatesTest, AStoppedUpdateIsAvailableAgain)
{
    m_fake->holdUpdate = true;
    Updates updates(bus());
    updates.load();
    ASSERT_TRUE(settlesIn(updates, Updates::State::Available));
    updates.update(true);
    ASSERT_TRUE(waitFor([&] { return updates.canCancel(); }));

    std::vector<Updates::State> states;
    Subscription changed = updates.changed.connect([&] { states.push_back(updates.state()); });
    updates.cancel();
    ASSERT_TRUE(settlesIn(updates, Updates::State::Available));
    EXPECT_EQ(m_fake->calls.back(), "Cancel");
    EXPECT_EQ(states, std::vector<Updates::State>{Updates::State::Available});
    EXPECT_EQ(updates.error(), "");
    EXPECT_EQ(updates.packages().size(), 2u);

    // Stopped before PackageKit had it, nothing runs.
    m_fake->calls.clear();
    updates.update(true);
    updates.cancel();
    ASSERT_TRUE(settlesIn(updates, Updates::State::Available));
    runFor(50ms);
    EXPECT_TRUE(m_fake->calls.empty());
}

TEST_F(UpdatesTest, FollowsAnUpdateAlreadyRunning)
{
    m_fake->startForeignUpdate();
    Updates updates(bus());
    int installed = 0;
    Subscription subscription = updates.installed.connect([&] { ++installed; });
    updates.load();
    ASSERT_TRUE(settlesIn(updates, Updates::State::Updating));
    ASSERT_TRUE(waitFor([&] { return updates.progress() == 10; }));
    EXPECT_TRUE(updates.canCancel());
    EXPECT_TRUE(std::find(m_fake->calls.begin(), m_fake->calls.end(), "GetUpdates 2") == m_fake->calls.end());

    m_fake->progress(kStatusInstall, 70, false);
    ASSERT_TRUE(waitFor([&] { return updates.phase() == Updates::Phase::Installing; }));
    m_fake->updates.clear();
    m_fake->finishUpdate();
    ASSERT_TRUE(settlesIn(updates, Updates::State::UpToDate));
    EXPECT_EQ(installed, 1);
}

TEST_F(UpdatesTest, AnUpdateGoneBeforeItIsFollowedIsNoInstall)
{
    m_fake->startVanishingUpdate();
    Updates updates(bus());
    int installed = 0;
    std::vector<Updates::State> states;
    Subscription installs = updates.installed.connect([&] { ++installed; });
    Subscription changed = updates.changed.connect([&] { states.push_back(updates.state()); });
    updates.load();
    ASSERT_TRUE(settlesIn(updates, Updates::State::Available));
    EXPECT_TRUE(std::find(states.begin(), states.end(), Updates::State::Updating) != states.end());
    EXPECT_EQ(installed, 0);
    EXPECT_EQ(updates.error(), "");
    EXPECT_EQ(m_fake->calls.back(), "GetUpdates 2");
}

TEST_F(UpdatesTest, AFailedBackgroundCheckChangesNothing)
{
    Updates updates(bus());
    updates.load();
    ASSERT_TRUE(settlesIn(updates, Updates::State::Available));
    const std::vector<Package> listed = updates.packages();

    std::vector<bool> finished;
    std::vector<Updates::State> states;
    Subscription checked = updates.checkFinished.connect([&](bool success) { finished.push_back(success); });
    Subscription changed = updates.changed.connect([&] { states.push_back(updates.state()); });
    m_fake->refreshError = "Cannot download the package lists";
    m_fake->hints.clear();
    updates.checkInBackground();
    EXPECT_TRUE(updates.isCheckingInBackground());
    EXPECT_EQ(updates.state(), Updates::State::Available);
    ASSERT_TRUE(waitFor([&] { return !finished.empty(); }));
    EXPECT_EQ(finished, std::vector<bool>{false});
    EXPECT_FALSE(updates.isCheckingInBackground());
    EXPECT_TRUE(states.empty());
    EXPECT_EQ(updates.state(), Updates::State::Available);
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
