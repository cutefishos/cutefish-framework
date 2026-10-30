// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace cutefish::accounts {

// The pictures every account can pick from, sorted by file name.
std::vector<std::string> presetPictures(const std::string& directory = "/usr/share/cutefish/avatars");

// One of the presets at random, empty when none is installed.
std::string randomPresetPicture(const std::string& directory = "/usr/share/cutefish/avatars");

// The colour, as 0xRRGGBB, an account without a picture shows: chosen from a
// small palette by the user name, so every surface shows the same one.
uint32_t pictureColor(const std::string& userName);

} // namespace cutefish::accounts
