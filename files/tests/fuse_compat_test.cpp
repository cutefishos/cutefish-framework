// SPDX-License-Identifier: BSD-3-Clause
#include <gtest/gtest.h>
#include <cerrno>
#include <cstddef>
#include <filesystem>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>
#include "cutefish/files/local_file_system.h"
#include "testing.h"

using namespace cutefish::files;
using namespace cutefish::files::testing;

// Only the forked child loses these optional filesystem operations. Its normal
// file operations still run against real temporary directories.
static bool cloudOperations(bool failSync = false, bool crossDevice = false)
{
    sock_filter filters[] = {
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(seccomp_data, nr)),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_renameat2, 0, 6),
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(seccomp_data, args)),
        BPF_STMT(BPF_MISC | BPF_TAX, 0),
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(seccomp_data, args) + 2 * sizeof(uint64_t)),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_X, 0, 0, 1),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | EINVAL),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | (crossDevice ? EXDEV : EINVAL)),
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(seccomp_data, nr)),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_linkat, 0, 1),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | EOPNOTSUPP),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_syncfs, 0, 1),
        BPF_STMT(BPF_RET | BPF_K, failSync ? SECCOMP_RET_ERRNO | EIO : SECCOMP_RET_ALLOW),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
    };
    sock_fprog program{static_cast<unsigned short>(sizeof(filters) / sizeof(filters[0])), filters};
    return ::prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) == 0
        && ::prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &program) == 0;
}

template<class Function> static void onCloud(Function function, bool failSync = false, bool crossDevice = false)
{
    const pid_t child = ::fork();
    ASSERT_GE(child, 0);
    if (child == 0) {
        if (!cloudOperations(failSync, crossDevice))
            ::_exit(77);
        try {
            function();
        } catch (const Failure& failure) {
            ADD_FAILURE() << failure.error.code << ": " << failure.error.path;
        } catch (...) {
            ADD_FAILURE() << "Unexpected exception";
        }
        ::_exit(::testing::Test::HasFailure() ? 1 : 0);
    }
    int status = 0;
    ASSERT_EQ(child, ::waitpid(child, &status, 0));
    ASSERT_TRUE(WIFEXITED(status));
    if (WEXITSTATUS(status) == 77)
        GTEST_SKIP() << "seccomp fault injection is unavailable";
    EXPECT_EQ(0, WEXITSTATUS(status));
}

TEST(FuseCompatibility, CopiesFilesDirectoriesAndSymlinksWithoutRenameFlags)
{
    TemporaryDirectory dir;
    ASSERT_TRUE(makePath(dir.filePath("source/sub")));
    writeFile(dir.filePath("source/sub/file"), "complete contents");
    ASSERT_EQ(0, ::symlink("sub/file", dir.filePath("source/link").c_str()));
    onCloud([&] {
        OperationContext ctx;
        EXPECT_EQ(dir.filePath("target"), LocalFileSystem::copy(dir.filePath("source"), dir.filePath("target"), ConflictPolicy::Fail, ctx));
        OperationContext file;
        EXPECT_EQ(dir.filePath("single"), LocalFileSystem::copy(dir.filePath("source/sub/file"), dir.filePath("single"), ConflictPolicy::Fail, file));
        EXPECT_EQ(17u, file.bytes);
        EXPECT_EQ(1u, file.files);
        OperationContext link;
        LocalFileSystem::link(dir.filePath("single"), dir.filePath("alias"), ConflictPolicy::Fail, link);
    });
    EXPECT_EQ("complete contents", readFile(dir.filePath("target/sub/file")));
    EXPECT_EQ("complete contents", readFile(dir.filePath("single")));
    EXPECT_TRUE(std::filesystem::is_symlink(dir.filePath("target/link")));
    EXPECT_TRUE(std::filesystem::is_symlink(dir.filePath("alias")));
    for (const auto& name : entries(dir.path())) EXPECT_FALSE(name.starts_with(".cutefish-"));
}

TEST(FuseCompatibility, ExistingTargetsAreNeverClobberedByFailSkipOrKeepBoth)
{
    TemporaryDirectory dir;
    writeFile(dir.filePath("source"), "source");
    writeFile(dir.filePath("target"), "keep");
    onCloud([&] {
        OperationContext fail, skip, both;
        EXPECT_EQ(EEXIST, errorCode([&] { LocalFileSystem::copy(dir.filePath("source"), dir.filePath("target"), ConflictPolicy::Fail, fail); }));
        EXPECT_TRUE(LocalFileSystem::copy(dir.filePath("source"), dir.filePath("target"), ConflictPolicy::Skip, skip).empty());
        EXPECT_EQ(dir.filePath("target (1)"), LocalFileSystem::copy(dir.filePath("source"), dir.filePath("target"), ConflictPolicy::KeepBoth, both));
    });
    EXPECT_EQ("keep", readFile(dir.filePath("target")));
    EXPECT_EQ("source", readFile(dir.filePath("target (1)")));
    EXPECT_EQ("source", readFile(dir.filePath("source")));
}

TEST(FuseCompatibility, SameFilesystemMoveFallsBackAndKeepsContents)
{
    TemporaryDirectory dir;
    ASSERT_TRUE(makePath(dir.filePath("source/sub")));
    writeFile(dir.filePath("source/sub/file"), "move me");
    onCloud([&] {
        OperationContext ctx;
        EXPECT_EQ(dir.filePath("target"), LocalFileSystem::move(dir.filePath("source"), dir.filePath("target"), ConflictPolicy::Fail, ctx));
    });
    EXPECT_EQ("move me", readFile(dir.filePath("target/sub/file")));
    EXPECT_FALSE(exists(dir.filePath("source")));
}

TEST(FuseCompatibility, FailedDestinationSyncPreservesMoveSource)
{
    TemporaryDirectory dir;
    writeFile(dir.filePath("source"), "only original");
    onCloud([&] {
        OperationContext ctx;
        EXPECT_EQ(EIO, errorCode([&] { LocalFileSystem::move(dir.filePath("source"), dir.filePath("target"), ConflictPolicy::Fail, ctx); }));
    }, true);
    EXPECT_EQ("only original", readFile(dir.filePath("source")));
    EXPECT_EQ("only original", readFile(dir.filePath("target")));
}

TEST(FuseCompatibility, CancelledFallbackDoesNotDeleteAConcurrentlyReplacedDestination)
{
    TemporaryDirectory dir;
    writeFile(dir.filePath("source"), std::string(2 * 1024 * 1024, 's'));
    writeFile(dir.filePath("foreign"), "keep foreign data");
    onCloud([&] {
        OperationContext ctx;
        bool replaced = false;
        ctx.progress = [&](uint64_t, uint64_t) {
            if (!replaced && exists(dir.filePath("target"))) {
                ASSERT_EQ(0, ::rename(dir.filePath("foreign").c_str(), dir.filePath("target").c_str()));
                replaced = true;
                ctx.canceled = true;
            }
        };
        EXPECT_EQ(ECANCELED, errorCode([&] { LocalFileSystem::move(dir.filePath("source"), dir.filePath("target"), ConflictPolicy::Fail, ctx); }));
        EXPECT_TRUE(replaced);
    });
    EXPECT_EQ(2 * 1024 * 1024, fileSize(dir.filePath("source")));
    EXPECT_EQ("keep foreign data", readFile(dir.filePath("target")));
    for (const auto& name : entries(dir.path())) EXPECT_FALSE(name.starts_with(".cutefish-"));
}

TEST(FuseCompatibility, DirectoryPublishFailurePreservesForeignEntriesAndMoveSource)
{
    TemporaryDirectory dir;
    ASSERT_TRUE(makePath(dir.filePath("source")));
    writeFile(dir.filePath("source/file"), "source data");
    onCloud([&] {
        OperationContext ctx;
        ctx.currentItem = [&](const std::string&) {
            if (exists(dir.filePath("target")) && !exists(dir.filePath("target/foreign")))
                writeFile(dir.filePath("target/foreign"), "keep");
        };
        const int error = errorCode([&] { LocalFileSystem::move(dir.filePath("source"), dir.filePath("target"), ConflictPolicy::Fail, ctx); });
        EXPECT_TRUE(error == ENOTEMPTY || error == EEXIST) << error;
    });
    EXPECT_EQ("source data", readFile(dir.filePath("source/file")));
    EXPECT_EQ("keep", readFile(dir.filePath("target/foreign")));
}

TEST(FuseCompatibility, CrossFilesystemMoveCopiesBeforeRemovingSource)
{
    TemporaryDirectory source;
    TemporaryDirectory destination("/dev/shm");
    ASSERT_TRUE(destination.isValid());
    struct stat a{}, b{};
    ASSERT_EQ(0, ::stat(source.path().c_str(), &a));
    ASSERT_EQ(0, ::stat(destination.path().c_str(), &b));
    if (a.st_dev == b.st_dev) GTEST_SKIP() << "No separate temporary filesystem";
    ASSERT_TRUE(makePath(source.filePath("tree/sub")));
    writeFile(source.filePath("tree/sub/file"), "cross-device");
    // Allow the first actual cross-device rename to report EXDEV, while the
    // destination publication uses the same unsupported-flags fallback.
    onCloud([&] {
        OperationContext ctx;
        EXPECT_EQ(destination.filePath("tree"), LocalFileSystem::move(source.filePath("tree"), destination.filePath("tree"), ConflictPolicy::Fail, ctx));
    }, false, true);
    EXPECT_FALSE(exists(source.filePath("tree")));
    EXPECT_EQ("cross-device", readFile(destination.filePath("tree/sub/file")));
}

TEST(FuseCompatibility, CancelledFallbackRemovesItsOwnPartialFileAndKeepsMoveSource)
{
    TemporaryDirectory dir;
    writeFile(dir.filePath("source"), std::string(2 * 1024 * 1024, 's'));
    onCloud([&] {
        OperationContext ctx;
        ctx.progress = [&](uint64_t, uint64_t) {
            if (exists(dir.filePath("target")))
                ctx.canceled = true;
        };
        EXPECT_EQ(ECANCELED, errorCode([&] { LocalFileSystem::move(dir.filePath("source"), dir.filePath("target"), ConflictPolicy::Fail, ctx); }));
    });
    EXPECT_EQ(2 * 1024 * 1024, fileSize(dir.filePath("source")));
    EXPECT_FALSE(exists(dir.filePath("target")));
    for (const auto& name : entries(dir.path())) EXPECT_FALSE(name.starts_with(".cutefish-"));
}

TEST(FuseCompatibility, AnExistingEmptyDirectoryIsNotReplacedWithoutPermission)
{
    TemporaryDirectory dir;
    ASSERT_TRUE(makePath(dir.filePath("source")));
    ASSERT_TRUE(makePath(dir.filePath("target")));
    writeFile(dir.filePath("source/file"), "source");
    struct stat before{}, after{};
    ASSERT_EQ(0, ::stat(dir.filePath("target").c_str(), &before));
    onCloud([&] {
        OperationContext ctx;
        EXPECT_EQ(EEXIST, errorCode([&] { LocalFileSystem::copy(dir.filePath("source"), dir.filePath("target"), ConflictPolicy::Fail, ctx); }));
    });
    ASSERT_EQ(0, ::stat(dir.filePath("target").c_str(), &after));
    EXPECT_EQ(before.st_ino, after.st_ino);
    EXPECT_TRUE(entries(dir.filePath("target")).empty());
    EXPECT_EQ("source", readFile(dir.filePath("source/file")));
}
