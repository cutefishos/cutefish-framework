// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include <string>

namespace cutefish::files::internal {

// The shared Applications folder, which only its group may change; others
// change it through org.cutefish.ApplicationsFolder once an administrator
// agreed. Each call blocks while the administrator is asked, and throws a
// Failure when refused.
namespace applications_folder {

// Whether `folder` is the shared Applications folder and the caller may not
// change it themselves.
bool needsHelper(const std::string& folder);

// A folder of the caller's own inside it, for a copy to be published.
std::string prepare();
// Puts the staged item under its name; its path.
std::string publish(const std::string& staging, const std::string& name, bool replace);
// Moves the item out into a new staging folder and hands it to the caller;
// that folder's path.
std::string release(const std::string& name);
void discard(const std::string& staging);

} // namespace applications_folder

} // namespace cutefish::files::internal
