// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
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

// The system's pending updates, listed and installed through PackageKit
// while the session runs. The getters never block.
class Updates
{
public:
    enum class State {
        // Nothing asked yet.
        Unknown,
        Checking,
        UpToDate,
        Available,
        Updating,
        Error,
    };

    enum class Phase {
        Downloading,
        Installing,
    };

    explicit Updates(base::dbus::Connection* bus = base::dbus::Connection::system());
    ~Updates();

    Updates(const Updates&) = delete;
    Updates& operator=(const Updates&) = delete;

    State state() const { return m_state; }
    bool isBusy() const { return m_state == State::Checking || m_state == State::Updating; }
    bool isCheckingInBackground() const { return m_background; }
    // Sorted by name.
    const std::vector<Package>& packages() const { return m_packages; }
    // While updating.
    Phase phase() const { return m_phase; }
    // While updating: from 0 to 100, -1 while unknown.
    int progress() const { return m_progress; }
    // While updating: whether cancel() can still stop it.
    bool canCancel() const { return m_canCancel; }
    // In the Error state: PackageKit's own text, which may be empty.
    const std::string& error() const { return m_error; }

    // Follows an update PackageKit is already running, as after this
    // process restarted in the middle of one; lists the updates the
    // package lists already know otherwise, without fetching them again.
    void load();
    // Fetches the package lists, then lists the updates. Ignored while busy.
    void check(bool interactive);
    // Checks without showing it: the state stays as it is until the check
    // went through, and a failed one changes nothing. Ignored while busy;
    // check() and update() take over from it.
    void checkInBackground();
    // Downloads and installs every listed update. Ignored unless updates are
    // available.
    void update(bool interactive);
    // Stops an update while canCancel(); the updates are available again.
    void cancel();

    base::Signal<> changed;
    // After check() or checkInBackground(), whether the lists were fetched
    // and the updates listed.
    base::Signal<bool> checkFinished;
    // An update went through, before the updates are listed again.
    base::Signal<> installed;

private:
    void findRunningUpdate(std::vector<std::string> paths);
    void follow(std::unique_ptr<Transaction> transaction);
    void list(bool refresh, bool interactive, bool background);
    void fetch(bool refresh, bool interactive);
    void setPackages(std::vector<Package> packages, bool checked);
    void stopBackgroundCheck();
    void setState(State state);
    void fail(const std::string& error, bool checked);

    base::dbus::Connection* m_bus;
    base::dbus::Proxy m_daemon;
    std::unique_ptr<Transaction> m_transaction;
    base::dbus::PendingCall m_lookupCall;
    State m_state = State::Unknown;
    std::vector<Package> m_packages;
    Phase m_phase = Phase::Downloading;
    int m_progress = -1;
    bool m_canCancel = false;
    std::string m_error;
    bool m_background = false;
};

} // namespace cutefish::packagekit
