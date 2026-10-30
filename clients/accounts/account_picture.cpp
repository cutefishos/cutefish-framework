// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#include "cutefish/accounts/account_picture.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <random>

namespace fs = std::filesystem;

namespace cutefish::accounts {

// Muted tones taken from the preset pictures.
static constexpr std::array<uint32_t, 8> kPictureColors{
        0xC4899B, // dusk rose
        0x8C86B4, // lavender
        0x5F9EA3, // teal
        0x86AE6E, // sage
        0xE2A04F, // amber
        0xC9714A, // terracotta
        0x6C87B3, // slate blue
        0x8E6E99, // plum
};

std::vector<std::string> presetPictures(const std::string& directory)
{
    std::vector<std::string> pictures;
    std::error_code error;
    for (const auto& entry : fs::directory_iterator(directory, error)) {
        if (entry.is_regular_file(error))
            pictures.push_back(entry.path().string());
    }
    std::sort(pictures.begin(), pictures.end());
    return pictures;
}

std::string randomPresetPicture(const std::string& directory)
{
    const std::vector<std::string> pictures = presetPictures(directory);
    if (pictures.empty())
        return {};
    std::random_device device;
    std::uniform_int_distribution<size_t> pick(0, pictures.size() - 1);
    return pictures[pick(device)];
}

uint32_t pictureColor(const std::string& userName)
{
    // FNV-1a: stable across processes and builds, unlike std::hash.
    uint32_t hash = 2166136261u;
    for (const unsigned char c : userName) {
        hash ^= c;
        hash *= 16777619u;
    }
    return kPictureColors[hash % kPictureColors.size()];
}

} // namespace cutefish::accounts
