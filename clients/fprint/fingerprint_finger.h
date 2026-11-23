// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include <optional>
#include <string_view>

namespace cutefish::fprint {

// The ten fingers fprintd keeps a print for, in its order.
enum class Finger {
    LeftThumb,
    LeftIndex,
    LeftMiddle,
    LeftRing,
    LeftLittle,
    RightThumb,
    RightIndex,
    RightMiddle,
    RightRing,
    RightLittle,
};

inline constexpr int kFingerCount = 10;

// fprintd's name for a finger, such as "right-index-finger".
std::string_view fingerName(Finger finger);
std::optional<Finger> fingerFromName(std::string_view name);

} // namespace cutefish::fprint
