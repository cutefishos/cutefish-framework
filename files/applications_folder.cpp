// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#include "applications_folder_p.h"

#include <cerrno>
#include <chrono>
#include <functional>
#include <memory>
#include <unistd.h>

#include "base/dbus.h"
#include "base/event_loop.h"
#include "cutefish/files/error.h"
#include "path_utils_p.h"

namespace cutefish::files::internal::applications_folder {

// An administrator may take a while to answer.
static constexpr std::chrono::minutes kTimeout(10);

static std::string& folderPath()
{
    static std::string path = "/Applications";
    return path;
}

const std::string& folder()
{
    return folderPath();
}

void setFolder(std::string path)
{
    folderPath() = std::move(path);
}

static base::dbus::Reply call(const std::string& method, const std::string& subject,
                              const std::function<void(base::dbus::Message&)>& arguments)
{
    base::EventLoop loop;
    std::unique_ptr<base::dbus::Connection> bus =
            base::dbus::Connection::open(base::dbus::Connection::Bus::System, &loop);
    if (!bus)
        throw Failure(EACCES, subject);
    base::dbus::Message message = bus->newMethodCall("org.cutefish.ApplicationsFolder",
                                                     "/org/cutefish/ApplicationsFolder",
                                                     "org.cutefish.ApplicationsFolder", method);
    arguments(message);
    base::dbus::Reply reply = bus->call(message, kTimeout);
    // A refused administrator cancels quietly, as no answer would.
    if (reply.isError() && reply.error().name == "org.freedesktop.DBus.Error.AccessDenied")
        throw Failure(ECANCELED, subject);
    if (reply.isError())
        throw Failure(EACCES, subject, reply.error().message);
    return reply;
}

static std::string answer(const base::dbus::Reply& reply, const std::string& subject)
{
    std::string path;
    if (!reply.read(path) || path.empty())
        throw Failure(EACCES, subject);
    return path;
}

void authorize(OperationContext& ctx)
{
    if (ctx.authorized)
        return;
    if (ctx.authorize && !ctx.authorize(folder()))
        throw Failure(ECANCELED, folder());
    ctx.authorized = true;
}

bool needsHelper(const std::string& path)
{
    return canonicalPath(path) == folder() && ::access(folder().c_str(), W_OK) != 0 && errno == EACCES;
}

std::string prepare()
{
    return answer(call("prepare", folder(), [](base::dbus::Message&) {}), folder());
}

std::string publish(const std::string& staging, const std::string& name, bool replace)
{
    const std::string subject = folder() + '/' + name;
    return answer(call("publish", subject,
                       [&](base::dbus::Message& message) { message.append(staging, name, replace); }),
                  subject);
}

std::string release(const std::string& name)
{
    const std::string subject = folder() + '/' + name;
    return answer(call("release", subject, [&](base::dbus::Message& message) { message.append(name); }), subject);
}

void discard(const std::string& staging)
{
    call("discard", staging, [&](base::dbus::Message& message) { message.append(staging); });
}

} // namespace cutefish::files::internal::applications_folder
