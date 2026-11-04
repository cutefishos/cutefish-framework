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

static constexpr char kFolder[] = "/Applications";

// An administrator may take a while to answer.
static constexpr std::chrono::minutes kTimeout(10);

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

bool needsHelper(const std::string& folder)
{
    return canonicalPath(folder) == kFolder && ::access(kFolder, W_OK) != 0 && errno == EACCES;
}

std::string prepare()
{
    return answer(call("prepare", kFolder, [](base::dbus::Message&) {}), kFolder);
}

std::string publish(const std::string& staging, const std::string& name, bool replace)
{
    const std::string subject = std::string(kFolder) + '/' + name;
    return answer(call("publish", subject,
                       [&](base::dbus::Message& message) { message.append(staging, name, replace); }),
                  subject);
}

std::string release(const std::string& name)
{
    const std::string subject = std::string(kFolder) + '/' + name;
    return answer(call("release", subject, [&](base::dbus::Message& message) { message.append(name); }), subject);
}

void discard(const std::string& staging)
{
    call("discard", staging, [&](base::dbus::Message& message) { message.append(staging); });
}

} // namespace cutefish::files::internal::applications_folder
