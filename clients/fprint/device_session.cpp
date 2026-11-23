// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#include "device_session.h"

#include <memory>

#include "base/event_loop.h"

using namespace cutefish::base;

namespace cutefish::fprint {

Failure failureFromError(const dbus::Error& error)
{
    if (error.name.ends_with(".PermissionDenied"))
        return Failure::NotAuthorized;
    if (error.name.ends_with(".AlreadyInUse"))
        return Failure::InUse;
    return Failure::Failed;
}

void releaseDevice(const dbus::Proxy& device, bool enrolling)
{
    if (!enrolling) {
        device.send("Release");
        return;
    }
    auto stop = std::make_shared<dbus::PendingCall>();
    *stop = device.callAsync("EnrollStop", [device, stop](const dbus::Reply&) {
        device.send("Release");
        // The reply that is running keeps its own slot alive until it returns.
        if (EventLoop* loop = EventLoop::current())
            loop->post([stop] {});
    });
}

} // namespace cutefish::fprint
