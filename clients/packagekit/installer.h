// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include <memory>
#include <string>

#include "base/dbus.h"
#include "base/signal.h"
#include "cutefish/packagekit/transaction.h"

namespace cutefish::packagekit {

// One package, found by name and installed with what it depends on through
// PackageKit. The getters never block.
class Installer
{
public:
    enum class State {
        Idle,
        // Looking the package up, which may fetch the package lists.
        Looking,
        Downloading,
        Installing,
        Installed,
        // No package source of this system has it.
        Unavailable,
        Error,
    };

    explicit Installer(base::dbus::Connection* bus = base::dbus::Connection::system());
    ~Installer();

    Installer(const Installer&) = delete;
    Installer& operator=(const Installer&) = delete;

    State state() const { return m_state; }
    bool isBusy() const
    {
        return m_state == State::Looking || m_state == State::Downloading || m_state == State::Installing;
    }
    // Of the download and the installation as one, never going back: from 0
    // to 100, -1 before the download starts.
    int progress() const { return m_progress; }
    // Whether cancel() can still stop it.
    bool canCancel() const { return m_installing ? m_canCancel : m_state == State::Looking; }
    // In the Error state: PackageKit's own text, which may be empty.
    const std::string& error() const { return m_error; }

    // Installs the package; one already installed is Installed at once.
    // The package lists are fetched once when they do not know the name,
    // as those of a source added since the last fetch. Ignored while busy.
    void install(const std::string& name);
    // Stops it while canCancel(); the state is Idle again, as it is when the
    // user turns the authorization down.
    void cancel();

    base::Signal<> changed;

private:
    void resolve(bool refreshed);
    void refresh();
    void installPackage(const std::string& id);
    void setState(State state);
    void fail(const std::string& error);

    base::dbus::Connection* m_bus;
    std::unique_ptr<Transaction> m_transaction;
    std::string m_name;
    State m_state = State::Idle;
    int m_progress = -1;
    bool m_installing = false;
    bool m_canCancel = false;
    std::string m_error;
};

} // namespace cutefish::packagekit
