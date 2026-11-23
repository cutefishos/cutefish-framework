// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#include "cutefish/fprint/fingerprint_finger.h"

#include <array>

namespace cutefish::fprint {

static constexpr std::array<std::string_view, kFingerCount> kNames = {
    "left-thumb",   "left-index-finger",  "left-middle-finger",  "left-ring-finger",  "left-little-finger",
    "right-thumb",  "right-index-finger", "right-middle-finger", "right-ring-finger", "right-little-finger",
};

std::string_view fingerName(Finger finger)
{
    return kNames[size_t(finger)];
}

std::optional<Finger> fingerFromName(std::string_view name)
{
    for (size_t i = 0; i < kNames.size(); ++i) {
        if (kNames[i] == name)
            return Finger(i);
    }
    return std::nullopt;
}

} // namespace cutefish::fprint
