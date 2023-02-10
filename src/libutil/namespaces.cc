#if __linux__

#include "namespaces.hh"
#include "util.hh"

namespace nix {

bool userNamespacesSupported()
{
    static bool res = [&]() -> bool
    {
        if (!pathExists("/proc/self/ns/user")) {
            notice("'/proc/self/ns/user' does not exist; your kernel was likely built without CONFIG_USER_NS=y, which is required for sandboxing");
            return false;
        }

        Path maxUserNamespaces = "/proc/sys/user/max_user_namespaces";
        if (!pathExists(maxUserNamespaces) ||
            trim(readFile(maxUserNamespaces)) == "0")
        {
            notice("user namespaces appear to be disabled; they are required for sandboxing; check '/proc/sys/user/max_user_namespaces'");
            return false;
        }

        Path procSysKernelUnprivilegedUsernsClone = "/proc/sys/kernel/unprivileged_userns_clone";
        if (pathExists(procSysKernelUnprivilegedUsernsClone)
            && trim(readFile(procSysKernelUnprivilegedUsernsClone)) == "0")
        {
            notice("user namespaces appear to be disabled; they are required for sandboxing; check '/proc/sys/kernel/unprivileged_userns_clone'");
            return false;
        }

        Pid pid = startProcess([&]()
        {
            auto res = unshare(CLONE_NEWUSER);
            _exit(res ? 1 : 0);
        });

        bool supported = pid.wait() == 0;

        if (!supported)
            debug("user namespaces do not work on this system");

        return supported;
    }();
    return res;
}

bool mountNamespacesSupported()
{
    static bool res = [&]() -> bool
    {
        bool useUserNamespace = userNamespacesSupported();

        Pid pid = startProcess([&]()
        {
            auto res = unshare(CLONE_NEWNS | (useUserNamespace ? CLONE_NEWUSER : 0));
            _exit(res ? 1 : 0);
        });

        bool supported = pid.wait() == 0;

        if (!supported)
            debug("mount namespaces do not work on this system");

        return supported;
    }();
    return res;
}

}

#endif
