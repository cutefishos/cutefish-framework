// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include <cstdint>
#include <string>

#include "cutefish/files/file_item.h"
#include "cutefish/files/file_system_url.h"
#include "cutefish/files/operation_context.h"

namespace cutefish::files {

// The operations themselves, blocking, on the calling thread. Each throws a
// Failure; copies and moves stage their output under a hidden name and leave
// nothing behind when they fail or are cancelled.
class LocalFileSystem
{
public:
    static FileItemList enumerate(const Url& url, bool hidden = true);
    static uint64_t directorySize(const std::string& path, OperationContext& context);

    // The path the item went to, or empty when skipped.
    static std::string copy(const std::string& source, const std::string& destination, ConflictPolicy policy,
                            OperationContext& context);
    static std::string link(const std::string& source, const std::string& destination, ConflictPolicy policy,
                            OperationContext& context);
    static std::string move(const std::string& source, const std::string& destination, ConflictPolicy policy,
                            OperationContext& context);
    static void remove(const std::string& path, OperationContext& context);
    static void remove(const Url& url, OperationContext& context);
    static void mkdir(const std::string& path);

    // Whether items may be added to the folder or taken out of it, by the
    // user or, for the shared Applications folder, once an administrator
    // agreed.
    static bool canChange(const std::string& folder);

    static Url trash(const Url& url, OperationContext& context);
    static Url restore(const Url& url, ConflictPolicy policy, OperationContext& context);
    static void emptyTrash(OperationContext& context);
};

} // namespace cutefish::files
