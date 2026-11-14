// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#include "cutefish/packagekit/updates.h"

#include <algorithm>

using namespace cutefish::base::dbus;

namespace cutefish::packagekit {

static constexpr const char* kService = "org.freedesktop.PackageKit";
static constexpr const char* kPath = "/org/freedesktop/PackageKit";
static constexpr const char* kTransactionInterface = "org.freedesktop.PackageKit.Transaction";

// PackageKit's bitfields hold 1 << the enum value: PK_FILTER_ENUM_NONE, and
// PK_TRANSACTION_FLAG_ENUM_ONLY_TRUSTED and ONLY_DOWNLOAD.
static constexpr uint64_t kFilterNone = uint64_t(1) << 1;
static constexpr uint64_t kOnlyTrusted = uint64_t(1) << 1;
static constexpr uint64_t kOnlyDownload = uint64_t(1) << 3;
// PK_ROLE_ENUM_UPDATE_PACKAGES.
static constexpr uint32_t kRoleUpdatePackages = 22;
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

// PK_STATUS_ENUM values of a transaction that has started changing the
// system: remove, install, update, cleanup, obsolete, test-commit, commit.
static bool changesTheSystem(uint32_t status)
{
    switch (status) {
    case 6:
    case 9:
    case 10:
    case 11:
    case 12:
    case 15:
    case 16:
        return true;
    default:
        return false;
    }
}

Updates::Updates(Connection* bus)
    : m_bus(bus)
    , m_daemon(bus, kService, kPath, kService)
{
}

Updates::~Updates() = default;

void Updates::load()
{
    if (isBusy() || !m_bus)
        return;
    stopBackgroundCheck();
    setState(State::Checking);
    m_lookupCall = m_daemon.callAsync("GetTransactionList", [this](const Reply& reply) {
        std::vector<std::string> paths;
        for (const ObjectPath& path : reply.value<std::vector<ObjectPath>>().value_or(std::vector<ObjectPath>()))
            paths.push_back(path.path);
        findRunningUpdate(std::move(paths));
    });
}

void Updates::check(bool interactive)
{
    list(true, interactive, false);
}

void Updates::checkInBackground()
{
    list(true, false, true);
}

void Updates::update(bool interactive)
{
    if (m_state != State::Available)
        return;
    stopBackgroundCheck();
    std::vector<std::string> ids;
    for (const Package& package : m_packages)
        ids.push_back(package.id);
    auto transaction = std::make_unique<Transaction>(m_bus, interactive);
    Transaction* started = transaction.get();
    follow(std::move(transaction));
    started->run("UpdatePackages", {Variant(kOnlyTrusted), Variant(ids)});
}

void Updates::cancel()
{
    if (m_state == State::Updating && m_transaction)
        m_transaction->cancel();
}

void Updates::findRunningUpdate(std::vector<std::string> paths)
{
    if (paths.empty()) {
        fetch(false, false);
        return;
    }
    const std::string path = paths.front();
    paths.erase(paths.begin());
    const Proxy transaction(m_bus, kService, path, kTransactionInterface);
    m_lookupCall = transaction.getAllAsync([this, path, paths = std::move(paths)](const Reply& reply) mutable {
        const VariantMap properties = reply.value<VariantMap>().value_or(VariantMap());
        const auto role = properties.find("Role");
        const auto flags = properties.find("TransactionFlags");
        const bool installs = role != properties.end() && role->second.value<uint32_t>() == kRoleUpdatePackages
                && (flags == properties.end() || !(flags->second.value<uint64_t>() & kOnlyDownload));
        if (!installs) {
            findRunningUpdate(std::move(paths));
            return;
        }
        auto followed = std::make_unique<Transaction>(m_bus, false);
        Transaction* running = followed.get();
        follow(std::move(followed));
        running->attach(path);
    });
}

void Updates::follow(std::unique_ptr<Transaction> transaction)
{
    m_phase = Phase::Downloading;
    m_progress = -1;
    m_canCancel = false;
    m_transaction = std::move(transaction);
    m_transaction->onChanged = [this] {
        m_progress = m_transaction->percentage();
        m_canCancel = m_transaction->canCancel();
        if (changesTheSystem(m_transaction->status()))
            m_phase = Phase::Installing;
        changed.emit();
    };
    m_transaction->onFinished = [this](const Transaction::Result& result) {
        const std::unique_ptr<Transaction> finished = std::move(m_transaction);
        m_canCancel = false;
        if (result.success) {
            installed.emit();
            fetch(false, false);
        } else if (result.cancelled || result.errorCode == Transaction::kErrorNotAuthorized) {
            // A followed update has no list yet.
            if (m_packages.empty())
                fetch(false, false);
            else
                setState(State::Available);
        } else {
            fail(result.error, false);
        }
    };
    setState(State::Updating);
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
    fetch(refresh, interactive);
}

void Updates::fetch(bool refresh, bool interactive)
{
    if (!m_background && m_state != State::Checking)
        setState(State::Checking);

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
                setPackages(std::move(*packages), refresh);
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
}

void Updates::setPackages(std::vector<Package> packages, bool checked)
{
    std::sort(packages.begin(), packages.end(),
              [](const Package& a, const Package& b) { return a.name() < b.name(); });
    m_background = false;
    m_packages = std::move(packages);
    m_error.clear();
    setState(m_packages.empty() ? State::UpToDate : State::Available);
    if (checked)
        checkFinished.emit(true);
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
