// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#include "cutefish/packagekit/installer.h"

#include <vector>

using namespace cutefish::base::dbus;

namespace cutefish::packagekit {

// PackageKit's bitfields hold 1 << the enum value: PK_FILTER_ENUM_NONE and
// PK_TRANSACTION_FLAG_ENUM_ONLY_TRUSTED.
static constexpr uint64_t kFilterNone = uint64_t(1) << 1;
static constexpr uint64_t kOnlyTrusted = uint64_t(1) << 1;
// PK_INFO_ENUM_INSTALLED.
static constexpr uint32_t kInfoInstalled = 1;

Installer::Installer(Connection* bus)
    : m_bus(bus)
{
}

Installer::~Installer() = default;

void Installer::install(const std::string& name)
{
    if (isBusy())
        return;
    if (!m_bus) {
        fail({});
        return;
    }
    m_name = name;
    m_error.clear();
    m_progress = -1;
    m_canCancel = false;
    setState(State::Looking);
    resolve(false);
}

void Installer::cancel()
{
    if (m_state == State::Looking) {
        m_transaction.reset();
        setState(State::Idle);
        return;
    }
    if (m_canCancel && m_transaction)
        m_transaction->cancel();
}

void Installer::resolve(bool refreshed)
{
    auto found = std::make_shared<std::vector<Package>>();
    m_transaction = std::make_unique<Transaction>(m_bus, true);
    m_transaction->onPackages = [found](const std::vector<Package>& packages) {
        found->insert(found->end(), packages.begin(), packages.end());
    };
    m_transaction->onFinished = [this, found, refreshed](const Transaction::Result& result) {
        const std::unique_ptr<Transaction> finished = std::move(m_transaction);
        if (!result.success) {
            fail(result.error);
            return;
        }
        for (const Package& package : *found) {
            if ((package.info & 0xffff) == kInfoInstalled) {
                setState(State::Installed);
                return;
            }
        }
        if (!found->empty())
            installPackage(found->front().id);
        else if (refreshed)
            setState(State::Unavailable);
        else
            refresh();
    };
    m_transaction->run("Resolve", {Variant(kFilterNone), Variant(std::vector<std::string>{m_name})});
}

void Installer::refresh()
{
    m_transaction = std::make_unique<Transaction>(m_bus, true);
    m_transaction->onFinished = [this](const Transaction::Result& result) {
        const std::unique_ptr<Transaction> finished = std::move(m_transaction);
        if (result.success)
            resolve(true);
        else if (result.errorCode == Transaction::kErrorNotAuthorized)
            setState(State::Idle);
        else
            fail(result.error);
    };
    m_transaction->run("RefreshCache", {Variant(false)});
}

void Installer::installPackage(const std::string& id)
{
    m_transaction = std::make_unique<Transaction>(m_bus, true);
    m_transaction->onChanged = [this] {
        m_progress = m_transaction->percentage();
        m_canCancel = m_transaction->canCancel();
        if (m_transaction->changesTheSystem())
            m_state = State::Installing;
        changed.emit();
    };
    m_transaction->onFinished = [this](const Transaction::Result& result) {
        const std::unique_ptr<Transaction> finished = std::move(m_transaction);
        m_canCancel = false;
        if (result.success)
            setState(State::Installed);
        else if (result.cancelled || result.errorCode == Transaction::kErrorNotAuthorized)
            setState(State::Idle);
        else
            fail(result.error);
    };
    setState(State::Downloading);
    m_transaction->run("InstallPackages", {Variant(kOnlyTrusted), Variant(std::vector<std::string>{id})});
}

void Installer::setState(State state)
{
    m_state = state;
    changed.emit();
}

void Installer::fail(const std::string& error)
{
    m_error = error;
    setState(State::Error);
}

} // namespace cutefish::packagekit
