// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/dbus.h"
#include "base/signal.h"
#include "cutefish/packagekit/transaction.h"

namespace cutefish::packagekit {

// Asks the bus once whether PackageKit runs there or can be started there;
// dropping it drops the answer.
class Availability
{
public:
    Availability(base::dbus::Connection* bus, std::function<void(bool available)> done);

private:
    base::dbus::Connection* m_bus;
    std::function<void(bool)> m_done;
    base::dbus::PendingCall m_call;
};

// The system's pending updates through PackageKit's offline updates: they
// are listed and downloaded while the session runs, and installed by the
// next boot once triggered. The getters never block.
class Updates
{
public:
    enum class State {
        // Nothing asked yet.
        Unknown,
        Checking,
        UpToDate,
        Available,
        Downloading,
        // Every listed update is downloaded and waits for the next boot.
        Ready,
        Error,
    };

    // What the boot that installed the updates reported.
    struct OfflineResult
    {
        bool success = false;
        std::vector<std::string> packageIds;
        // Seconds since the epoch.
        uint64_t timeFinished = 0;
        std::string error;
    };

    explicit Updates(base::dbus::Connection* bus = base::dbus::Connection::system());
    ~Updates();

    Updates(const Updates&) = delete;
    Updates& operator=(const Updates&) = delete;

    State state() const { return m_state; }
    bool isBusy() const { return m_state == State::Checking || m_state == State::Downloading; }
    bool isCheckingInBackground() const { return m_background; }
    // Sorted by name.
    const std::vector<Package>& packages() const { return m_packages; }
    // While downloading: from 0 to 100, -1 while unknown.
    int progress() const { return m_progress; }
    // In the Error state: PackageKit's own text, which may be empty.
    const std::string& error() const { return m_error; }

    // Lists the updates the package lists already know, without fetching
    // them again.
    void load();
    // Fetches the package lists, then lists the updates. Ignored while busy.
    void check(bool interactive);
    // Checks without showing it: the state stays as it is until the check
    // went through, and a failed one changes nothing. Ignored while busy;
    // check() and download() take over from it.
    void checkInBackground();
    // Downloads every listed update for the next boot to install. Ignored
    // unless updates are available.
    void download(bool interactive);
    // Stops a download; what came already stays in the package cache, and
    // the updates are available again.
    void cancelDownload();
    // Makes the next boot install the downloaded updates, then restart.
    [[nodiscard]] base::dbus::PendingCall trigger(std::function<void(bool success, const std::string& error)> done);

    // Nothing when there is no result to read.
    [[nodiscard]] base::dbus::PendingCall readOfflineResult(
            std::function<void(const std::optional<OfflineResult>&)> done);
    void clearOfflineResult();

    base::Signal<> changed;
    // After check() or checkInBackground(), whether the lists were fetched
    // and the updates listed.
    base::Signal<bool> checkFinished;

private:
    void list(bool refresh, bool interactive, bool background);
    void readPrepared(std::vector<Package> packages, bool checked);
    void stopBackgroundCheck();
    void setState(State state);
    void fail(const std::string& error, bool checked);

    base::dbus::Connection* m_bus;
    base::dbus::Proxy m_offline;
    std::unique_ptr<Transaction> m_transaction;
    base::dbus::PendingCall m_preparedCall;
    State m_state = State::Unknown;
    std::vector<Package> m_packages;
    int m_progress = -1;
    std::string m_error;
    bool m_background = false;
};

} // namespace cutefish::packagekit
