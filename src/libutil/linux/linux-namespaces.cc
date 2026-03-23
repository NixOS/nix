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
#include <sys/statvfs.h>

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

//////////////////////////////////////////////////////////////////////

static AutoCloseFD fdSavedMountNamespace;
static AutoCloseFD fdSavedRoot;
static bool havePrivateMountNs = false;

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

void tryEnterPrivateMountNamespace()
{
    try {
        saveMountNamespace();
        if (unshare(CLONE_NEWNS) == -1)
            throw SysError("setting up a private mount namespace");
        havePrivateMountNs = true;
    } catch (Error & e) {
        debug("failed to set up a private mount namespace: %s", e.message());
    }
}

void remountReadOnlyWritable(const std::filesystem::path & path)
{
    struct statvfs stat;
    if (statvfs(path.c_str(), &stat) != 0)
        throw SysError("getting mount info for %s", PathFmt(path));

    if (!(stat.f_flag & ST_RDONLY))
        return;

    if (!havePrivateMountNs)
        throw Error(
            "cannot remount %s writable: not in a private mount namespace, "
            "so the remount would affect the host mount table. "
            "This usually happens inside containers or user namespaces where unshare(CLONE_NEWNS) is not permitted",
            PathFmt(path));

    /* In a user namespace, mount flags like `nodev` and `nosuid` are
       locked and dropping them causes `EPERM`, so here we translate each
       `statvfs` flag to the corresponding `mount` flag individually. */
    unsigned long flags = MS_REMOUNT | MS_BIND;
    if (stat.f_flag & ST_NODEV)
        flags |= MS_NODEV;
    if (stat.f_flag & ST_NOSUID)
        flags |= MS_NOSUID;
    if (stat.f_flag & ST_NOEXEC)
        flags |= MS_NOEXEC;
    if (stat.f_flag & ST_NOATIME)
        flags |= MS_NOATIME;
    if (stat.f_flag & ST_NODIRATIME)
        flags |= MS_NODIRATIME;
    if (stat.f_flag & ST_RELATIME)
        flags |= MS_RELATIME;
    if (stat.f_flag & ST_SYNCHRONOUS)
        flags |= MS_SYNCHRONOUS;
#ifdef ST_NOSYMFOLLOW
    if (stat.f_flag & ST_NOSYMFOLLOW)
        flags |= MS_NOSYMFOLLOW;
#endif
    if (mount(0, path.c_str(), "none", flags, 0) == -1)
        throw SysError("remounting %s writable", PathFmt(path));
}

void restoreMountNamespace()
{
    if (!havePrivateMountNs)
        return;

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
