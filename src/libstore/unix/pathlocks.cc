#include "nix/store/pathlocks.hh"
#include "nix/util/file-system-at.hh"
#include "nix/util/file-system.hh"
#include "nix/util/signals.hh"
#include "nix/util/util.hh"

#include "../pathlocks-internal.hh"

#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <limits>
#include <vector>

#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#ifdef __APPLE__
#  include <bsm/libbsm.h>
#  include <libproc.h>
#  include <mach/mach.h>
#  include <mach/task_info.h>
#  include <sys/xattr.h>
#endif

#ifdef __linux__
#  include <sys/syscall.h>
#endif

namespace nix {

void MissingLockOwner::anchor() {}

AutoCloseFD openLockFile(const std::filesystem::path & path, bool create)
{
    AutoCloseFD fd;

    fd = open(path.c_str(), O_CLOEXEC | O_RDWR | (create ? O_CREAT : 0), 0600);
    if (!fd && (create || errno != ENOENT))
        throw SysError("opening lock file %1%", PathFmt(path));

    return fd;
}

void deleteLockFile(const std::filesystem::path & path, Descriptor desc)
{
    /* Get rid of the lock file.  Have to be careful not to introduce
       races.  Write a (meaningless) token to the file to indicate to
       other processes waiting on this lock that the lock is stale
       (deleted). */
    tryUnlink(path);
    writeFull(desc, "d");
    /* We just try to unlink don't care if it fails; removing the lock
       file is an optimisation, not a necessity. */
}

bool lockFile(Descriptor desc, LockType lockType, bool wait)
{
    int type;
    if (lockType == ltRead)
        type = LOCK_SH;
    else if (lockType == ltWrite)
        type = LOCK_EX;
    else if (lockType == ltNone)
        type = LOCK_UN;
    else
        unreachable();

    if (wait) {
        while (flock(desc, type) != 0) {
            checkInterrupt();
            if (errno != EINTR)
                throw SysError("acquiring/releasing lock");
            else
                return false;
        }
    } else {
        while (flock(desc, type | LOCK_NB) != 0) {
            checkInterrupt();
            if (errno == EWOULDBLOCK)
                return false;
            if (errno != EINTR)
                throw SysError("acquiring/releasing lock");
        }
    }

    return true;
}

#ifdef __APPLE__
static constexpr std::string_view lockOwnerAttr = "org.nixos.nix.lock-owner";

struct FileLockOwnerRecord
{
    uint32_t version = 1;
    uint32_t reserved = 0;
    FileLockOwner owner;
    audit_token_t auditToken;
};

static std::filesystem::path getOwnerLockPath(const std::filesystem::path & path)
{
    auto ownerPath = path;
    ownerPath += ".owner";
    return ownerPath;
}
#endif

#ifdef __APPLE__
static std::optional<FileLockOwner> getProcessIdentity(uint64_t pid)
{
    if (pid == 0 || pid > static_cast<uint64_t>(std::numeric_limits<pid_t>::max()))
        throw MissingLockOwner("invalid process ID %d", pid);

    proc_bsdinfo info{};
    auto size = proc_pidinfo(static_cast<pid_t>(pid), PROC_PIDTBSDINFO, 0, &info, sizeof(info));
    if (size == 0 && errno == ESRCH)
        return std::nullopt;
    if (size != sizeof(info))
        throw SysError("reading identity of process %d", pid);

    return FileLockOwner{
        .pid = pid,
        .startTime = info.pbi_start_tvsec,
        .startTimeFraction = info.pbi_start_tvusec,
    };
}
#endif

#ifdef __APPLE__
static bool setFileLockOwner(Descriptor fd)
{
    auto identity = getProcessIdentity(getpid());
    if (!identity) {
        debug("cannot record the owner of a lock: current process disappeared");
        return false;
    }

    FileLockOwnerRecord record{.owner = *identity};
    mach_msg_type_number_t count = TASK_AUDIT_TOKEN_COUNT;
    auto result =
        task_info(mach_task_self(), TASK_AUDIT_TOKEN, reinterpret_cast<task_info_t>(&record.auditToken), &count);
    if (result != KERN_SUCCESS || count != TASK_AUDIT_TOKEN_COUNT) {
        debug("cannot record the owner of a lock: cannot read the current process audit token");
        return false;
    }

    result = fsetxattr(fd, lockOwnerAttr.data(), &record, sizeof(record), 0, 0);
    if (result == -1) {
        debug("cannot record the owner of a lock: %s", strerror(errno));
        return false;
    }
    return true;
}

static void clearFileLockOwner(Descriptor fd)
{
    if (fremovexattr(fd, lockOwnerAttr.data(), 0) == -1 && errno != ENOATTR)
        debug("cannot clear the owner of a lock: %s", strerror(errno));
}
#endif

#ifdef __APPLE__
static FileLockOwnerRecord readFileLockOwnerRecord(Descriptor fd)
{
    FileLockOwnerRecord record;
    auto size = fgetxattr(fd, lockOwnerAttr.data(), &record, sizeof(record), 0, 0);
    if (size == -1) {
        if (errno == ENOATTR || errno == ENODATA)
            throw MissingLockOwner("the lock owner did not record its process identity");
        if (errno == ERANGE)
            throw MissingLockOwner("the lock owner recorded an invalid process identity");
        throw SysError("reading the owner of a lock");
    }

    if (size != sizeof(record) || record.version != 1 || record.reserved != 0)
        throw MissingLockOwner("the lock owner recorded an invalid process identity");

    auto pid = audit_token_to_pid(record.auditToken);
    if (pid <= 0 || static_cast<uint64_t>(pid) != record.owner.pid)
        throw MissingLockOwner("the lock owner recorded an invalid process identity");

    return record;
}
#endif

#ifndef __linux__
static FileLockOwner readFileLockOwner(Descriptor fd)
{
#  ifdef __APPLE__
    return readFileLockOwnerRecord(fd).owner;
#  else
    (void) fd;
    throw MissingLockOwner("lock owner metadata is not supported on this platform");
#  endif
}

#  ifdef __APPLE__
static void validateKernelLockOwner(const std::filesystem::path & path, const FileLockOwner & owner)
{
    /* POSIX record locks are process-scoped. Avoid opening the companion file
       when querying our own lock, since closing that descriptor would release
       every record lock this process holds on the file. */
    if (owner.pid == static_cast<uint64_t>(getpid()))
        return;

    auto fd = openLockFile(getOwnerLockPath(path), false);
    if (!fd)
        throw MissingLockOwner("the lock owner did not create its kernel owner lock");

    struct flock lock{
        .l_start = 0,
        .l_len = 0,
        .l_pid = 0,
        .l_type = F_WRLCK,
        .l_whence = SEEK_SET,
    };
    if (fcntl(fd.get(), F_GETLK, &lock) == -1)
        throw SysError("reading the kernel owner of a lock");
    if (lock.l_type != F_WRLCK || lock.l_pid <= 0 || static_cast<uint64_t>(lock.l_pid) != owner.pid)
        throw MissingLockOwner("the recorded owner does not hold the kernel owner lock");
}
#  endif
#endif

#ifdef __linux__
static std::optional<FileLockOwner> getProcessIdentity(uint64_t pid)
{
    if (pid == 0 || pid > static_cast<uint64_t>(std::numeric_limits<pid_t>::max()))
        throw MissingLockOwner("invalid process ID %d", pid);

    std::string status;
    try {
        status = readFile(std::filesystem::path{"/proc"} / std::to_string(pid) / "stat");
    } catch (SystemError & e) {
        if (e.is(std::errc::no_such_file_or_directory))
            return std::nullopt;
        throw;
    }

    /* The second field is parenthesised and may contain spaces or closing
       parentheses. Fields following its final closing parenthesis have an
       unambiguous layout; starttime is field 22. */
    auto endOfName = status.rfind(')');
    if (endOfName == std::string::npos)
        throw MissingLockOwner("process %d has an invalid /proc stat record", pid);
    auto fields = tokenizeString<std::vector<std::string>>(std::string_view{status}.substr(endOfName + 1));
    if (fields.size() <= 19)
        throw MissingLockOwner("process %d has an invalid /proc stat record", pid);
    auto startTime = string2Int<uint64_t>(fields[19]);
    if (!startTime)
        throw MissingLockOwner("process %d has an invalid /proc stat record", pid);

    return FileLockOwner{.pid = pid, .startTime = *startTime};
}

static bool matchesFileLock(
    const std::vector<std::string> & fields,
    size_t typeIndex,
    size_t modeIndex,
    size_t deviceIndex,
    const struct stat & st)
{
    if (fields.size() <= deviceIndex || fields[typeIndex] != "FLOCK" || fields[modeIndex] != "WRITE")
        return false;

    auto lastColon = fields[deviceIndex].rfind(':');
    if (lastColon == std::string::npos)
        return false;

    return string2Int<uint64_t>(std::string_view{fields[deviceIndex]}.substr(lastColon + 1)) == st.st_ino;
}

struct FileLockIdentity
{
    struct stat st;
    uint64_t mountId;
};

static FileLockIdentity getFileLockIdentity(Descriptor fd)
{
    for (auto line : tokenizeString<std::vector<std::string>>(
             readFile(std::filesystem::path{"/proc/self/fdinfo"} / std::to_string(fd)), "\n")) {
        auto fields = tokenizeString<std::vector<std::string>>(line);
        if (fields.size() == 2 && fields[0] == "mnt_id:")
            if (auto mountId = string2Int<uint64_t>(fields[1]))
                return {.st = nix::fstat(fd), .mountId = *mountId};
    }
    throw MissingLockOwner("the lock file descriptor has no mount ID");
}

static bool processHoldsFileLock(const std::filesystem::path & process, const FileLockIdentity & identity)
{
    for (auto & entry : DirectoryIterator{process / "fdinfo"}) {
        bool matchingLock = false;
        bool matchingMount = false;
        bool matchingInode = false;
        try {
            for (auto line : tokenizeString<std::vector<std::string>>(readFile(entry.path()), "\n")) {
                auto fields = tokenizeString<std::vector<std::string>>(line);
                if (fields.size() >= 7 && fields[0] == "lock:")
                    matchingLock = matchingLock || matchesFileLock(fields, 2, 4, 6, identity.st);
                else if (fields.size() == 2 && fields[0] == "mnt_id:")
                    matchingMount = string2Int<uint64_t>(fields[1]) == identity.mountId;
                else if (fields.size() == 2 && fields[0] == "ino:")
                    matchingInode = string2Int<uint64_t>(fields[1]) == identity.st.st_ino;
            }
        } catch (SystemError & e) {
            if (e.is(std::errc::no_such_file_or_directory))
                continue;
            throw;
        }
        if (matchingLock && matchingMount && matchingInode)
            return true;
    }
    return false;
}

static std::optional<FileLockOwner> getFileLockOwnerLinux(Descriptor fd)
{
    auto identity = getFileLockIdentity(fd);

    for (auto line : tokenizeString<std::vector<std::string>>(readFile("/proc/locks"), "\n")) {
        auto fields = tokenizeString<std::vector<std::string>>(line);
        if (fields.size() < 6 || !matchesFileLock(fields, 1, 3, 5, identity.st))
            continue;

        auto pid = string2Int<uint64_t>(fields[4]);
        if (!pid || *pid == 0 || *pid > static_cast<uint64_t>(std::numeric_limits<pid_t>::max()))
            continue;

        auto process = std::filesystem::path{"/proc"} / std::to_string(*pid);
        try {
            auto processIdentity = getProcessIdentity(*pid);
            if (processIdentity && processHoldsFileLock(process, identity)
                && getProcessIdentity(*pid) == processIdentity)
                return processIdentity;
        } catch (SystemError & e) {
            if (!e.is(std::errc::no_such_file_or_directory) && !e.is(std::errc::permission_denied))
                throw;
        }
    }

    return std::nullopt;
}
#endif

std::optional<FileLockOwner> getFileLockOwner(const std::filesystem::path & path)
{
    auto fd = openLockFile(path, false);
    if (!fd)
        return {};

    FdLock probe(fd.get(), ltWrite, false, "");
    if (probe.acquired)
        return {};

#ifdef __linux__
    auto owner = getFileLockOwnerLinux(fd.get());
    if (owner)
        return owner;

    FdLock retry(fd.get(), ltWrite, false, "");
    if (retry.acquired)
        return {};
    throw MissingLockOwner("the process holding the lock on %s is not visible in /proc/locks", PathFmt(path));
#else
    auto owner = readFileLockOwner(fd.get());
#  ifdef __APPLE__
    if (getProcessIdentity(owner.pid) != owner)
        throw MissingLockOwner("process %d recorded as the lock owner no longer exists", owner.pid);
    validateKernelLockOwner(path, owner);
#  endif
    if (readFileLockOwner(fd.get()) != owner)
        throw MissingLockOwner("the owner of the lock on %s changed while it was being inspected", PathFmt(path));
#  ifdef __APPLE__
    validateKernelLockOwner(path, owner);
#  endif

    return owner;
#endif
}

bool fileLockOwnerStillHoldsLock(const std::filesystem::path & path, const FileLockOwner & owner)
{
#ifdef __linux__
    try {
        auto fd = openLockFile(path, false);
        if (!fd || getProcessIdentity(owner.pid) != owner)
            return false;
        return processHoldsFileLock(
            std::filesystem::path{"/proc"} / std::to_string(owner.pid), getFileLockIdentity(fd.get()));
    } catch (SystemError & e) {
        if (e.is(std::errc::no_such_file_or_directory))
            return false;
        throw;
    }
#elif defined(__APPLE__)
    try {
        return getFileLockOwner(path) == owner;
    } catch (MissingLockOwner &) {
        try {
            validateKernelLockOwner(path, owner);
            return getProcessIdentity(owner.pid) == owner;
        } catch (MissingLockOwner &) {
            return false;
        }
    }
#else
    (void) path;
    (void) owner;
    return false;
#endif
}

bool signalFileLockOwner(const std::filesystem::path & path, const FileLockOwner & owner, int signal)
{
#ifdef __linux__
#  if defined(SYS_pidfd_open) && defined(SYS_pidfd_send_signal)
    auto rawPidFd = syscall(SYS_pidfd_open, static_cast<pid_t>(owner.pid), 0);
    if (rawPidFd == -1) {
        if (errno == ESRCH || errno == ENOENT || errno == ENODEV)
            return false;
        if (errno == ENOSYS)
            throw UsageError("terminating builds requires pidfd support from the Linux kernel");
        throw SysError("opening a pidfd for process %d", owner.pid);
    }
    AutoCloseFD pidFd(static_cast<int>(rawPidFd));

    if (!fileLockOwnerStillHoldsLock(path, owner))
        return false;

    if (syscall(SYS_pidfd_send_signal, pidFd.get(), signal, nullptr, 0) == -1) {
        if (errno == ESRCH)
            return false;
        if (errno == ENOSYS)
            throw UsageError("terminating builds requires pidfd support from the Linux kernel");
        throw SysError("sending signal %d to process %d", signal, owner.pid);
    }
    return true;
#  else
    (void) path;
    (void) owner;
    (void) signal;
    throw UsageError("terminating builds requires a Nix build with Linux pidfd support");
#  endif
#elif defined(__APPLE__)
    auto fd = openLockFile(path, false);
    if (!fd)
        return false;
    auto record = readFileLockOwnerRecord(fd.get());
    if (record.owner != owner)
        return false;

    if (!fileLockOwnerStillHoldsLock(path, owner))
        return false;
    auto error = proc_signal_with_audittoken(&record.auditToken, signal);
    if (error != 0) {
        if (error == ESRCH)
            return false;
        throw SysError(error, "sending signal %d to process %d", signal, owner.pid);
    }
    return true;
#else
    (void) path;
    (void) owner;
    (void) signal;
    throw UsageError("build termination is not supported on this platform");
#endif
}

#ifdef __APPLE__
static bool lockOwnerFile(Descriptor fd)
{
    struct flock lock{
        .l_start = 0,
        .l_len = 0,
        .l_pid = 0,
        .l_type = F_WRLCK,
        .l_whence = SEEK_SET,
    };
    while (fcntl(fd, F_SETLK, &lock) == -1) {
        if (errno == EINTR) {
            checkInterrupt();
            continue;
        }
        if (errno == EACCES || errno == EAGAIN)
            return false;
        debug("cannot acquire a path lock owner record: %s", strerror(errno));
        return false;
    }
    return true;
}
#endif

bool PathLocks::lockPaths(
    const std::set<std::filesystem::path> & paths, const std::string & waitMsg, bool wait, LockOwnerTracking trackOwner)
{
    assert(fds.empty());
    (void) trackOwner;

    /* Note that `fds' is built incrementally so that the destructor
       will only release those locks that we have already acquired. */

    /* Acquire the lock for each path in sorted order. This ensures
       that locks are always acquired in the same order, thus
       preventing deadlocks. */
    for (auto & path : paths) {
        checkInterrupt();
        auto lockPath = path;
        lockPath += ".lock";

        debug("locking path %1%", PathFmt(path));

        AutoCloseFD fd;
#ifdef __APPLE__
        AutoCloseFD ownerFd;
#endif

        while (1) {

            /* Open/create the lock file. */
            fd = openLockFile(lockPath, true);

            /* Acquire an exclusive lock. */
            if (!lockFile(fd.get(), ltWrite, false)) {
                if (wait) {
                    if (waitMsg != "")
                        printError(waitMsg);
                    lockFile(fd.get(), ltWrite, true);
                } else {
                    /* Failed to lock this path; release all other
                       locks. */
                    unlock();
                    return false;
                }
            }

            debug("lock acquired on %1%", PathFmt(lockPath));

            /* Check that the lock file hasn't become stale (i.e.,
               hasn't been unlinked). */
            auto st = nix::fstat(fd.get());
            if (st.st_size != 0)
                /* This lock file has been unlinked, so we're holding
                   a lock on a deleted file.  This means that other
                   processes may create and acquire a lock on
                   `lockPath', and proceed.  So we must retry. */
                debug("open lock file %1% has become stale", PathFmt(lockPath));
            else {
#ifdef __APPLE__
                if (trackOwner == LockOwnerTracking::Yes) {
                    auto ownerPath = getOwnerLockPath(lockPath);
                    clearFileLockOwner(fd.get());
                    tryUnlink(ownerPath);
                    try {
                        ownerFd = openLockFile(ownerPath, true);
                    } catch (SystemError & e) {
                        debug("cannot create a path lock owner record: %s", e.what());
                    }
                    if (ownerFd && lockOwnerFile(ownerFd.get())) {
                        try {
                            if (!setFileLockOwner(fd.get()))
                                ownerFd.close();
                        } catch (Error & e) {
                            debug("cannot record the owner of a lock: %s", e.what());
                            ownerFd.close();
                        }
                    } else
                        ownerFd.close();
                }
#endif
                break;
            }
        }

#ifdef __APPLE__
        if (ownerFd)
            ownerFds.emplace_back(ownerFd.release(), getOwnerLockPath(lockPath));
#endif
        /* Use borrow so that the descriptor isn't closed. */
        fds.push_back(FDPair(fd.release(), lockPath));
    }

    return true;
}

void PathLocks::unlock()
{
#ifdef __APPLE__
    if (!ownerFds.empty())
        for (auto & [fd, _] : fds)
            clearFileLockOwner(fd);

    for (auto & [fd, path] : ownerFds) {
        tryUnlink(path);
        if (close(fd) == -1)
            printError("error (ignored): cannot close lock owner record on %1%", PathFmt(path));
    }
    ownerFds.clear();
#endif

    for (auto & i : fds) {
        if (deletePaths)
            deleteLockFile(i.second, i.first);

        if (close(i.first) == -1)
            printError("error (ignored): cannot close lock file on %1%", PathFmt(i.second));

        debug("lock released on %1%", PathFmt(i.second));
    }

    fds.clear();
}

FdLock::FdLock(Descriptor desc, LockType lockType, bool wait, std::string_view waitMsg)
    : desc(desc)
{
    if (wait) {
        if (!lockFile(desc, lockType, false)) {
            printInfo("%s", waitMsg);
            acquired = lockFile(desc, lockType, true);
        }
    } else
        acquired = lockFile(desc, lockType, false);
}

} // namespace nix
