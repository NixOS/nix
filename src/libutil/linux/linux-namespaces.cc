#include "nix/util/linux-namespaces.hh"
#include "nix/util/current-process.hh"
#include "nix/util/util.hh"
#include "nix/util/finally.hh"
#include "nix/util/file-system.hh"
#include "nix/util/processes.hh"
#include "nix/util/signals.hh"

#include <mutex>
#include <sys/resource.h>
#include "nix/util/cgroup.hh"

#include <sys/mount.h>

namespace nix {

bool userNamespacesSupported()
{
    static auto res = [&]() -> bool {
        if (!pathExists("/proc/self/ns/user")) {
            debug("'/proc/self/ns/user' does not exist; your kernel was likely built without CONFIG_USER_NS=y");
            return false;
        }

        std::filesystem::path maxUserNamespaces = "/proc/sys/user/max_user_namespaces";
        if (!pathExists(maxUserNamespaces) || trim(readFile(maxUserNamespaces)) == "0") {
            debug("user namespaces appear to be disabled; check '/proc/sys/user/max_user_namespaces'");
            return false;
        }

        std::filesystem::path procSysKernelUnprivilegedUsernsClone = "/proc/sys/kernel/unprivileged_userns_clone";
        if (pathExists(procSysKernelUnprivilegedUsernsClone)
            && trim(readFile(procSysKernelUnprivilegedUsernsClone)) == "0") {
            debug("user namespaces appear to be disabled; check '/proc/sys/kernel/unprivileged_userns_clone'");
            return false;
        }

        try {
            Pid pid = startProcess([&]() { _exit(0); }, {.cloneFlags = CLONE_NEWUSER});

            auto r = pid.wait();
            /* The assert is OK because if we cannot do CLONE_NEWUSER we will
               throw above, and if the process does run, it must exit this way
               (or something else is really wrong). */
            assert(statusOk(r));
        } catch (SysError & e) {
            debug("user namespaces do not work on this system: %s", e.msg());
            return false;
        }

        return true;
    }();
    return res;
}

bool mountAndPidNamespacesSupported()
{
    static auto res = [&]() -> bool {
        try {

            Pid pid = startProcess(
                [&]() {
                    /* Make sure we don't remount the parent's /proc. */
                    if (mount(0, "/", 0, MS_PRIVATE | MS_REC, 0) == -1)
                        _exit(1);

                    /* Test whether we can remount /proc. The kernel disallows
                       this if /proc is not fully visible, i.e. if there are
                       filesystems mounted on top of files inside /proc.  See
                       https://lore.kernel.org/lkml/87tvsrjai0.fsf@xmission.com/T/. */
                    if (mount("none", "/proc", "proc", 0, 0) == -1)
                        _exit(2);

                    _exit(0);
                },
                {.cloneFlags = CLONE_NEWNS | CLONE_NEWPID | (userNamespacesSupported() ? CLONE_NEWUSER : 0)});

            if (auto status = pid.wait(); !statusOk(status)) {
                debug("PID namespaces do not work on this system: cannot remount /proc: %s", statusToString(status));
                return false;
            }

        } catch (SysError & e) {
            debug("mount namespaces do not work on this system: %s", e.msg());
            return false;
        }

        return true;
    }();
    return res;
}

bool binfmtMiscUserNamespacesSupported()
{
    static auto res = [&]() -> bool {
        if (!userNamespacesSupported())
            return false;

        try {
            auto uid = getuid();
            auto gid = getgid();

            Pid pid = startProcess(
                [&]() {
                    /* We can't unshare(CLONE_NEWUSER) again if our UID and GID
                       are not mapped, so set up a proper mapping. */
                    writeFile("/proc/self/uid_map", fmt("%d %d %d", 0, uid, 1));
                    writeFile("/proc/self/setgroups", "deny");
                    writeFile("/proc/self/gid_map", fmt("%d %d %d", 0, gid, 1));

                    /* Make sure we don't remount the parent's /proc. */
                    if (mount(0, "/", 0, MS_PRIVATE | MS_REC, 0) == -1)
                        _exit(1);

                    /* Mount binfmt_misc, which should create the instance
                       associated with the current userns.

                       Older kernels shouldn't even get past this, but we keep
                       going to complete a sanity check. */
                    if (mount("none", "/proc/sys/fs/binfmt_misc", "binfmt_misc", 0, 0) == -1)
                        throw SysError("mounting /proc/sys/fs/binfmt_misc");

                    auto dev1 = stat("/proc/sys/fs/binfmt_misc/register").st_dev;

                    /* New user namespace for new binfmt_misc, and new mount
                       namespace so that we can mount stuff while in the new
                       userns. */
                    if (unshare(CLONE_NEWUSER | CLONE_NEWNS) == -1)
                        throw SysError("unsharing user and mount namespaces");

                    /* Mount binfmt_misc again, which should be the instance
                       associated with the new userns and different from the
                       previous one */
                    if (mount("none", "/proc/sys/fs/binfmt_misc", "binfmt_misc", 0, 0) == -1)
                        throw SysError("mounting /proc/sys/fs/binfmt_misc again");

                    auto dev2 = stat("/proc/sys/fs/binfmt_misc/register").st_dev;

                    /* Two different binfmt_misc instances should have two
                       different st_dev, since they're different filesystems. */
                    if (dev1 == dev2)
                        _exit(1);

                    /* Creating multiple instances of binfmt_misc works. Yay! */
                    _exit(0);
                },
                {.cloneFlags = CLONE_NEWNS | CLONE_NEWUSER});

            if (pid.wait()) {
                debug("binfmt_misc user namespace sandboxing does not work");
                return false;
            }

        } catch (SysError & e) {
            debug("binfmt_misc user namespace sandboxing does not work: %s", e.msg());
            return false;
        }

        return true;
    }();

    return res;
}

//////////////////////////////////////////////////////////////////////

static AutoCloseFD fdSavedMountNamespace;
static AutoCloseFD fdSavedRoot;

void saveMountNamespace()
{
    static std::once_flag done;
    std::call_once(done, []() {
        fdSavedMountNamespace = open("/proc/self/ns/mnt", O_RDONLY);
        if (!fdSavedMountNamespace)
            throw SysError("saving parent mount namespace");

        fdSavedRoot = open("/proc/self/root", O_RDONLY);
    });
}

void restoreMountNamespace()
{
    try {
        auto savedCwd = std::filesystem::current_path();

        if (fdSavedMountNamespace && setns(fdSavedMountNamespace.get(), CLONE_NEWNS) == -1)
            throw SysError("restoring parent mount namespace");

        if (fdSavedRoot) {
            if (fchdir(fdSavedRoot.get()))
                throw SysError("chdir into saved root");
            if (chroot("."))
                throw SysError("chroot into saved root");
        }

        if (chdir(savedCwd.c_str()) == -1)
            throw SysError("restoring cwd");
    } catch (Error & e) {
        debug(e.msg());
    }
}

void tryUnshareFilesystem()
{
    if (unshare(CLONE_FS) != 0 && errno != EPERM && errno != ENOSYS)
        throw SysError("unsharing filesystem state");
}

} // namespace nix
