#if __linux__

#include "namespaces.hh"
#include "util.hh"
#include "finally.hh"

#include <mntent.h>

namespace nix {

bool userNamespacesSupported()
{
    static auto res = [&]() -> bool
    {
        if (!pathExists("/proc/self/ns/user")) {
            debug("'/proc/self/ns/user' does not exist; your kernel was likely built without CONFIG_USER_NS=y");
            return false;
        }

        Path maxUserNamespaces = "/proc/sys/user/max_user_namespaces";
        if (!pathExists(maxUserNamespaces) ||
            trim(readFile(maxUserNamespaces)) == "0")
        {
            debug("user namespaces appear to be disabled; check '/proc/sys/user/max_user_namespaces'");
            return false;
        }

        Path procSysKernelUnprivilegedUsernsClone = "/proc/sys/kernel/unprivileged_userns_clone";
        if (pathExists(procSysKernelUnprivilegedUsernsClone)
            && trim(readFile(procSysKernelUnprivilegedUsernsClone)) == "0")
        {
            debug("user namespaces appear to be disabled; check '/proc/sys/kernel/unprivileged_userns_clone'");
            return false;
        }

        Pid pid = startProcess([&]()
        {
            auto res = unshare(CLONE_NEWUSER);
            _exit(res ? 1 : 0);
        });

        return pid.wait() == 0;
    }();
    return res;
}

bool mountNamespacesSupported()
{
    static auto res = [&]() -> bool
    {
        bool useUserNamespace = userNamespacesSupported();

        Pid pid = startProcess([&]()
        {
            auto res = unshare(CLONE_NEWNS | (useUserNamespace ? CLONE_NEWUSER : 0));
            _exit(res ? 1 : 0);
        });

        return pid.wait() == 0;
    }();
    return res;
}

bool pidNamespacesSupported()
{
    static auto res = [&]() -> bool
    {
        /* Check whether /proc is fully visible, i.e. there are no
           filesystems mounted on top of files inside /proc. If this
           is not the case, then we cannot mount a new /proc inside
           the sandbox that matches the sandbox's PID namespace.
           See https://lore.kernel.org/lkml/87tvsrjai0.fsf@xmission.com/T/. */
        auto fp = fopen("/proc/mounts", "r");
        if (!fp) return false;
        Finally delFP = [&]() { fclose(fp); };

        while (auto ent = getmntent(fp))
            if (hasPrefix(std::string_view(ent->mnt_dir), "/proc/")) {
                debug("PID namespaces do not work because /proc is not fully visible; disabling sandboxing");
                return false;
            }

        return true;
    }();
    return res;
}

}

#endif
