// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#include "cutefish/packagekit/transaction.h"

#include <tuple>

using namespace cutefish::base::dbus;

namespace cutefish::packagekit {

static constexpr const char* kService = "org.freedesktop.PackageKit";
static constexpr const char* kPath = "/org/freedesktop/PackageKit";
static constexpr const char* kTransactionInterface = "org.freedesktop.PackageKit.Transaction";

// PK_EXIT_ENUM_SUCCESS and PK_EXIT_ENUM_CANCELLED.
static constexpr uint32_t kExitSuccess = 1;
static constexpr uint32_t kExitCancelled = 3;
// The Percentage property's value while the progress is unknown.
static constexpr uint32_t kPercentageUnknown = 101;

static std::string idField(const std::string& id, int index)
{
    size_t start = 0;
    for (int i = 0; i < index; ++i) {
        start = id.find(';', start);
        if (start == std::string::npos)
            return {};
        ++start;
    }
    return id.substr(start, id.find(';', start) - start);
}

std::string Package::name() const
{
    return idField(id, 0);
}

std::string Package::version() const
{
    return idField(id, 1);
}

Transaction::Transaction(Connection* bus, bool interactive)
    : m_bus(bus)
    , m_interactive(interactive)
{
}

Transaction::~Transaction() = default;

void Transaction::run(std::string method, Variant::Items arguments)
{
    m_method = std::move(method);
    m_arguments = std::move(arguments);
    if (!m_bus) {
        finish({});
        return;
    }

    Message create = m_bus->newMethodCall(kService, kPath, kService, "CreateTransaction");
    m_createCall = m_bus->callAsync(create, [this](const Reply& reply) {
        const std::optional<ObjectPath> path = reply.value<ObjectPath>();
        if (!path) {
            finish({false, false, reply.error().message});
            return;
        }
        start(path->path);
    });
}

void Transaction::start(const std::string& path)
{
    m_transaction = Proxy(m_bus, kService, path, kTransactionInterface);

    // PackageKit 1.3 sends the results in one Packages signal; older
    // versions send a Package signal for each.
    m_matches.push_back(m_transaction.onSignal("Packages", [this](const Message& message) {
        std::vector<std::tuple<uint32_t, std::string, std::string>> entries;
        if (!message.read(entries) || !onPackages)
            return;
        std::vector<Package> packages;
        for (auto& [info, id, summary] : entries)
            packages.push_back({std::move(id), std::move(summary), info});
        onPackages(packages);
    }));
    m_matches.push_back(m_transaction.onSignal("Package", [this](const Message& message) {
        Package package;
        if (message.read(package.info, package.id, package.summary) && onPackages)
            onPackages({package});
    }));
    m_matches.push_back(m_transaction.onSignal("ErrorCode", [this](const Message& message) {
        uint32_t code = 0;
        std::string details;
        if (message.read(code, details))
            m_error = details;
    }));
    m_matches.push_back(m_transaction.onSignal("Finished", [this](const Message& message) {
        uint32_t exit = 0;
        message.read(exit);
        const bool success = exit == kExitSuccess;
        const bool cancelled = exit == kExitCancelled;
        finish({success, cancelled, success || cancelled ? std::string() : m_error});
    }));
    m_matches.push_back(m_transaction.onPropertiesChanged([this](const VariantMap& changed,
                                                                 const std::vector<std::string>&) {
        const auto it = changed.find("Percentage");
        if (it == changed.end())
            return;
        const uint32_t value = it->second.value<uint32_t>();
        const int percentage = value >= kPercentageUnknown ? -1 : int(value);
        if (percentage == m_percentage)
            return;
        m_percentage = percentage;
        if (onPercentageChanged)
            onPercentageChanged();
    }));

    const std::vector<std::string> hints = m_interactive
            ? std::vector<std::string>{"interactive=true"}
            : std::vector<std::string>{"interactive=false", "background=true"};
    m_hintsCall = m_transaction.callAsync("SetHints", [this, path](const Reply&) {
        if (m_cancelled) {
            finish({false, true, {}});
            return;
        }
        Message call = m_bus->newMethodCall(kService, path, kTransactionInterface, m_method);
        for (const Variant& argument : m_arguments)
            call.appendAs(argument);
        call.setAllowInteractiveAuthorization(m_interactive);
        m_runCall = m_bus->callAsync(call, [this](const Reply& reply) {
            if (reply.isError())
                finish({false, false, reply.error().message});
        });
    }, hints);
}

void Transaction::cancel()
{
    if (m_finished || m_cancelled)
        return;
    m_cancelled = true;
    // Until the method runs there is nothing on the daemon to stop.
    if (m_runCall.isNull())
        return;
    m_cancelCall = m_transaction.callAsync("Cancel", [this](const Reply& reply) {
        if (reply.isError())
            m_cancelled = false;
    });
}

void Transaction::finish(Result result)
{
    if (m_finished)
        return;
    m_finished = true;
    m_matches.clear();
    // Held here, so the transaction may be destroyed while it runs.
    const std::function<void(const Result&)> done = std::move(onFinished);
    if (done)
        done(result);
}

} // namespace cutefish::packagekit
