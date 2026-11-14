// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "base/dbus.h"

namespace cutefish::packagekit {

struct Package
{
    // PackageKit's id: "name;version;arch;repository".
    std::string id;
    std::string summary;
    // PK_INFO_ENUM in the low 16 bits; PackageKit 1.3 puts the update's
    // severity in the high ones.
    uint32_t info = 0;

    std::string name() const;
    std::string version() const;
};

// One PackageKit transaction: created on the daemon, given its hints, then
// one method run on it; or one already running there, followed from now
// on. The getters never block.
class Transaction
{
public:
    // PK_ERROR_ENUM_NOT_AUTHORIZED: polkit refused, or the user dismissed
    // its password prompt.
    static constexpr uint32_t kErrorNotAuthorized = 48;

    struct Result
    {
        bool success = false;
        // Stopped by cancel().
        bool cancelled = false;
        // From the ErrorCode signal, or the refused call's D-Bus error;
        // empty on success.
        std::string error;
        // The ErrorCode signal's PK_ERROR_ENUM; 0 without one.
        uint32_t errorCode = 0;
    };

    // An interactive transaction may ask the user to authorize it through
    // the session's polkit agent; any other is refused instead, and runs
    // with background priority.
    Transaction(base::dbus::Connection* bus, bool interactive);
    ~Transaction();

    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;

    // Runs one method of org.freedesktop.PackageKit.Transaction, once.
    void run(std::string method, base::dbus::Variant::Items arguments);
    // Follows the transaction at `path` instead, whoever started it.
    void attach(const std::string& path);
    // Asks PackageKit to stop the method; it finishes as cancelled unless it
    // was done already.
    void cancel();

    // From 0 to 100; -1 while PackageKit does not know.
    int percentage() const { return m_percentage; }
    // PK_STATUS_ENUM.
    uint32_t status() const { return m_status; }
    // Whether PackageKit can still stop the transaction.
    bool canCancel() const { return m_canCancel; }

    std::function<void(const std::vector<Package>&)> onPackages;
    // The percentage, the status or whether it can be cancelled changed.
    std::function<void()> onChanged;
    // The last call; it may destroy the transaction.
    std::function<void(const Result&)> onFinished;

private:
    void start(const std::string& path);
    void update(const base::dbus::VariantMap& properties);
    void finish(Result result);

    base::dbus::Connection* m_bus;
    bool m_interactive;
    std::string m_method;
    base::dbus::Variant::Items m_arguments;
    base::dbus::Proxy m_transaction;
    std::vector<base::dbus::Match> m_matches;
    base::dbus::PendingCall m_createCall;
    base::dbus::PendingCall m_hintsCall;
    base::dbus::PendingCall m_runCall;
    base::dbus::PendingCall m_propertiesCall;
    base::dbus::PendingCall m_cancelCall;
    std::string m_error;
    uint32_t m_errorCode = 0;
    int m_percentage = -1;
    uint32_t m_status = 0;
    bool m_canCancel = false;
    bool m_finished = false;
    bool m_cancelled = false;
    bool m_attached = false;
};

} // namespace cutefish::packagekit
