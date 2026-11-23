// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include "base/dbus.h"
#include "cutefish/fprint/fingerprint_enrollment.h"

namespace cutefish::fprint {

inline constexpr const char* kService = "net.reactivated.Fprint";
inline constexpr const char* kDeviceInterface = "net.reactivated.Fprint.Device";

// The failure an fprintd error stands for.
Failure failureFromError(const base::dbus::Error& error);

// Gives a claimed reader back, after stopping the enrollment on it when one
// runs: fprintd refuses a release while an action is in progress, so the
// release waits for the stop's answer. Goes on after the caller is gone.
void releaseDevice(const base::dbus::Proxy& device, bool enrolling);

} // namespace cutefish::fprint
