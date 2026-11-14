// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#include "cutefish/packagekit/updates.h"

#include <algorithm>
#include <chrono>
#include <set>

using namespace cutefish::base::dbus;

namespace cutefish::packagekit {

static constexpr const char* kService = "org.freedesktop.PackageKit";
static constexpr const char* kPath = "/org/freedesktop/PackageKit";
static constexpr const char* kOfflineInterface = "org.freedesktop.PackageKit.Offline";

// PackageKit's bitfields hold 1 << the enum value: PK_FILTER_ENUM_NONE, and
// PK_TRANSACTION_FLAG_ENUM_ONLY_TRUSTED with ONLY_DOWNLOAD.
static constexpr uint64_t kFilterNone = uint64_t(1) << 1;
static constexpr uint64_t kOnlyTrustedDownload = (uint64_t(1) << 1) | (uint64_t(1) << 3);
// PK_INFO_ENUM_BLOCKED: held back, so it cannot be installed with the rest.
static constexpr uint32_t kInfoBlocked = 9;

Availability::Availability(Connection* bus, std::function<void(bool)> done)
    : m_bus(bus)
    , m_done(std::move(done))
{
    if (!m_bus) {
        m_done(false);
        return;
    }
    Proxy daemon(m_bus, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus");
    m_call = daemon.callAsync("NameHasOwner", [this, daemon](const Reply& reply) {
        if (reply.value<bool>().value_or(false)) {
            m_done(true);
            return;
        }
        m_call = daemon.callAsync("ListActivatableNames", [this](const Reply& reply) {
            const std::vector<std::string> names = reply.value<std::vector<std::string>>().value_or(
                    std::vector<std::string>());
            m_done(std::find(names.begin(), names.end(), kService) != names.end());
        });
    }, std::string(kService));
}

Updates::Updates(Connection* bus)
    : m_bus(bus)
    , m_offline(bus, kService, kPath, kOfflineInterface)
{
    m_offline.setInteractiveAuthorizationAllowed(true);
    // Long enough for the user to answer polkit's dialog.
    m_offline.setTimeout(std::chrono::minutes(2));
}

Updates::~Updates() = default;

void Updates::load()
{
    list(false, false, false);
}

void Updates::check(bool interactive)
{
    list(true, interactive, false);
}

void Updates::checkInBackground()
{
    list(true, false, true);
}

void Updates::list(bool refresh, bool interactive, bool background)
{
    if (isBusy() || !m_bus || (background && m_background))
        return;
    if (background) {
        m_background = true;
    } else {
        stopBackgroundCheck();
        setState(State::Checking);
    }

    auto packages = std::make_shared<std::vector<Package>>();
    const auto getUpdates = [this, packages, interactive, refresh] {
        m_transaction = std::make_unique<Transaction>(m_bus, interactive);
        m_transaction->onPackages = [packages](const std::vector<Package>& found) {
            for (const Package& package : found) {
                if ((package.info & 0xffff) != kInfoBlocked)
                    packages->push_back(package);
            }
        };
        m_transaction->onFinished = [this, packages, refresh](const Transaction::Result& result) {
            const std::unique_ptr<Transaction> finished = std::move(m_transaction);
            if (result.success)
                readPrepared(std::move(*packages), refresh);
            else
                fail(result.error, refresh);
        };
        m_transaction->run("GetUpdates", {Variant(kFilterNone)});
    };

    if (!refresh) {
        getUpdates();
        return;
    }
    m_transaction = std::make_unique<Transaction>(m_bus, interactive);
    m_transaction->onFinished = [this, getUpdates](const Transaction::Result& result) {
        const std::unique_ptr<Transaction> finished = std::move(m_transaction);
        if (result.success)
            getUpdates();
        else
            fail(result.error, true);
    };
    m_transaction->run("RefreshCache", {Variant(false)});
}

void Updates::stopBackgroundCheck()
{
    if (!m_background)
        return;
    m_background = false;
    m_transaction.reset();
    m_preparedCall.reset();
}

void Updates::download(bool interactive)
{
    if (m_state != State::Available)
        return;
    stopBackgroundCheck();
    std::vector<std::string> ids;
    for (const Package& package : m_packages)
        ids.push_back(package.id);
    m_progress = -1;
    setState(State::Downloading);

    m_transaction = std::make_unique<Transaction>(m_bus, interactive);
    m_transaction->onPercentageChanged = [this] {
        m_progress = m_transaction->percentage();
        changed.emit();
    };
    m_transaction->onFinished = [this](const Transaction::Result& result) {
        const std::unique_ptr<Transaction> finished = std::move(m_transaction);
        if (result.success)
            readPrepared(m_packages, false);
        else if (result.cancelled)
            setState(State::Available);
        else
            fail(result.error, false);
    };
    m_transaction->run("UpdatePackages", {Variant(kOnlyTrustedDownload), Variant(ids)});
}

void Updates::cancelDownload()
{
    if (m_state == State::Downloading && m_transaction)
        m_transaction->cancel();
}

PendingCall Updates::trigger(std::function<void(bool success, const std::string& error)> done)
{
    return m_offline.callAsync("Trigger", [done = std::move(done)](const Reply& reply) {
        done(!reply.isError(), reply.error().message);
    }, std::string("reboot"));
}

PendingCall Updates::readOfflineResult(std::function<void(const std::optional<OfflineResult>&)> done)
{
    return m_offline.callAsync("GetResults", [done = std::move(done)](const Reply& reply) {
        OfflineResult result;
        uint32_t role = 0;
        uint32_t errorCode = 0;
        // An error reply: no result is stored.
        if (!reply.read(result.success, result.packageIds, role, result.timeFinished, errorCode, result.error)) {
            done(std::nullopt);
            return;
        }
        if (result.success)
            result.error.clear();
        done(result);
    });
}

void Updates::clearOfflineResult()
{
    m_offline.send("ClearResults");
}

void Updates::readPrepared(std::vector<Package> packages, bool checked)
{
    std::sort(packages.begin(), packages.end(),
              [](const Package& a, const Package& b) { return a.name() < b.name(); });
    // Still Checking or Downloading until the answer.
    m_preparedCall = m_offline.callAsync("GetPrepared", [this, packages = std::move(packages),
                                                         checked](const Reply& reply) {
        const std::vector<std::string> ids = reply.value<std::vector<std::string>>().value_or(
                std::vector<std::string>());
        const std::set<std::string> prepared(ids.begin(), ids.end());
        const bool ready = !packages.empty() && std::all_of(packages.begin(), packages.end(), [&](const Package& p) {
            return prepared.count(p.id) != 0;
        });
        m_background = false;
        m_packages = packages;
        m_error.clear();
        setState(packages.empty() ? State::UpToDate : ready ? State::Ready : State::Available);
        if (checked)
            checkFinished.emit(true);
    });
}

void Updates::setState(State state)
{
    m_state = state;
    changed.emit();
}

void Updates::fail(const std::string& error, bool checked)
{
    if (m_background) {
        m_background = false;
    } else {
        m_error = error;
        setState(State::Error);
    }
    if (checked)
        checkFinished.emit(false);
}

} // namespace cutefish::packagekit
