// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>

#include "cutefish/files/error.h"

namespace cutefish::files {

enum class ConflictPolicy {
    Fail,
    Skip,
    KeepBoth,
    Replace,
    Ask
};

struct OperationContext
{
    std::atomic_bool canceled{false};
    const std::atomic_bool* cancelFlag = nullptr;
    std::function<void(const std::string&)> currentItem;
    uint64_t bytes = 0;
    uint64_t files = 0;
    // Space the regular files found by a size scan occupy, without sparse holes.
    uint64_t allocated = 0;
    std::function<void(uint64_t, uint64_t)> progress;
    std::function<ConflictPolicy(const std::string& source, const std::string& destination)> conflict;
    // Asked once, before the operation first changes a folder only an
    // administrator may change; false cancels it. Unset goes on, and the
    // system asks for the administrator.
    std::function<bool(const std::string& folder)> authorize;
    bool authorized = false;
    // Set once a Replace merges into an existing folder, which then holds more than was copied.
    bool merged = false;

    void checkpoint(const std::string& path = {}) const;
    void advance(uint64_t amount);
};

} // namespace cutefish::files
