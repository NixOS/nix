#include <gtest/gtest.h>

#include "nix/store/pathlocks.hh"
#include "pathlocks-internal.hh"
#include "nix/util/file-system.hh"
#include "nix/util/finally.hh"
#include "nix/util/processes.hh"
#include "nix/util/util.hh"

#ifndef _WIN32
#  include <csignal>

#  include <sys/wait.h>
#  include <unistd.h>
#endif

#ifdef __APPLE__
#  include <fcntl.h>
#  include <libproc.h>
#  include <sys/xattr.h>
#endif

namespace nix {

#if defined(__linux__) || defined(__APPLE__)

TEST(FileLockOwners, excludesProcessesThatOnlyHaveTheFileOpen)
{
    auto tempDir = createTempDir();
    AutoDelete deleteTempDir(tempDir, true);
    auto lockPath = tempDir / "build.lock";

    Pipe ready;
    ready.create();
    Pipe finish;
    finish.create();

    Pid child = startProcess([&] {
        ready.readSide.close();
        finish.writeSide.close();

        PathLocks owner({tempDir / "build"}, "", LockOwnerTracking::Yes);
        auto lockOwner = getFileLockOwner(lockPath);
        if (!lockOwner || lockOwner->pid != static_cast<uint64_t>(getpid()))
            _exit(1);
        writeFull(ready.writeSide.get(), "1", false);

        char ignored;
        readFull(finish.readSide.get(), &ignored, 1);
        _exit(0);
    });

    ready.writeSide.close();
    finish.readSide.close();

    char ignored;
    readFull(ready.readSide.get(), &ignored, 1);

    auto openOnlyFd = openLockFile(lockPath, false);
    ASSERT_TRUE(openOnlyFd);
    auto owner = getFileLockOwner(lockPath);
    ASSERT_TRUE(owner);
    EXPECT_EQ(owner->pid, static_cast<uint64_t>(static_cast<pid_t>(child)));

    auto staleOwner = *owner;
    ++staleOwner.startTime;
    EXPECT_FALSE(signalFileLockOwner(lockPath, staleOwner, SIGKILL));
    EXPECT_EQ(::kill(static_cast<pid_t>(child), 0), 0);

    EXPECT_TRUE(signalFileLockOwner(lockPath, *owner, SIGTERM));
    auto status = child.wait();
    EXPECT_TRUE(WIFSIGNALED(status));
    EXPECT_EQ(WTERMSIG(status), SIGTERM);
}

#  ifdef __linux__
TEST(FileLockOwners, refusesToGuessAtAnInheritedLockOwner)
{
    auto tempDir = createTempDir();
    AutoDelete deleteTempDir(tempDir, true);
    auto lockPath = tempDir / "build.lock";

    Pipe ready;
    ready.create();

    Pid child = startProcess([&] {
        ready.readSide.close();
        PathLocks owner({tempDir / "build"}, "", LockOwnerTracking::Yes);

        auto descendant = fork();
        if (descendant == -1)
            _exit(1);
        if (descendant == 0) {
            ready.writeSide.close();
            while (true)
                pause();
        }

        writeLine(ready.writeSide.get(), std::to_string(descendant));
        _exit(0);
    });

    ready.writeSide.close();
    auto descendant = string2Int<pid_t>(readLine(ready.readSide.get()));
    ASSERT_TRUE(descendant);
    Finally cleanup([&] { ::kill(*descendant, SIGKILL); });

    EXPECT_EQ(child.wait(), 0);
    EXPECT_THROW(getFileLockOwner(lockPath), MissingLockOwner);
}
#  endif

#  ifdef __APPLE__
TEST(FileLockOwners, replacesAStaleCompanionLock)
{
    auto tempDir = createTempDir();
    AutoDelete deleteTempDir(tempDir, true);
    auto path = tempDir / "build";
    auto lockPath = tempDir / "build.lock";
    auto ownerPath = tempDir / "build.lock.owner";

    Pipe ready;
    ready.create();
    Pipe finish;
    finish.create();

    Pid child = startProcess([&] {
        ready.readSide.close();
        finish.writeSide.close();

        auto lockFd = openLockFile(lockPath, true);
        auto ownerFd = openLockFile(ownerPath, true);
        struct flock lock{
            .l_start = 0,
            .l_len = 0,
            .l_pid = 0,
            .l_type = F_WRLCK,
            .l_whence = SEEK_SET,
        };
        if (fcntl(ownerFd.get(), F_SETLK, &lock) == -1)
            _exit(1);

        proc_bsdinfo info{};
        if (proc_pidinfo(getpid(), PROC_PIDTBSDINFO, 0, &info, sizeof(info)) != sizeof(info))
            _exit(1);
        auto metadata = fmt("%d:%d:%d", getpid(), info.pbi_start_tvsec, info.pbi_start_tvusec);
        if (fsetxattr(lockFd.get(), "org.nixos.nix.lock-owner", metadata.data(), metadata.size(), 0, 0) == -1)
            _exit(1);

        writeFull(ready.writeSide.get(), "1", false);
        char ignored;
        readFull(finish.readSide.get(), &ignored, 1);
        _exit(0);
    });

    ready.writeSide.close();
    finish.readSide.close();
    char ignored;
    readFull(ready.readSide.get(), &ignored, 1);

    PathLocks owner({path}, "", LockOwnerTracking::Yes);
    auto current = getFileLockOwner(lockPath);
    ASSERT_TRUE(current);
    EXPECT_EQ(current->pid, static_cast<uint64_t>(getpid()));

    owner.unlock();
    EXPECT_TRUE(pathExists(lockPath));
    EXPECT_FALSE(pathExists(ownerPath));

    writeFull(finish.writeSide.get(), "1", false);
    EXPECT_EQ(child.wait(), 0);
}
#  endif

#endif

#ifndef _WIN32

#  ifdef __linux__
TEST(FileLockOwners, readsKernelOwnerWithoutMetadata)
#  else
TEST(FileLockOwners, refusesToGuessWhenOwnerMetadataIsMissing)
#  endif
{
    auto tempDir = createTempDir();
    AutoDelete deleteTempDir(tempDir, true);
    auto lockPath = tempDir / "build.lock";

    auto ownerFd = openLockFile(lockPath, true);
    ASSERT_TRUE(lockFile(ownerFd.get(), ltWrite, false));

#  ifdef __linux__
    ASSERT_TRUE(getFileLockOwner(lockPath));
    EXPECT_EQ(getFileLockOwner(lockPath)->pid, static_cast<uint64_t>(getpid()));
#  else
    EXPECT_THROW(getFileLockOwner(lockPath), MissingLockOwner);
#  endif
}

#endif

} // namespace nix
