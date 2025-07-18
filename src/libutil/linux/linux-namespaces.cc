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

#include <grp.h>
#include <sys/mount.h>

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

        try {
            Pid pid = startProcess([&]()
            {
                _exit(0);
            }, {
                .cloneFlags = CLONE_NEWUSER
            });

            auto r = pid.wait();
            assert(!r);
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
    static auto res = [&]() -> bool
    {
        try {

            Pid pid = startProcess([&]()
            {
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
            }, {
                .cloneFlags = CLONE_NEWNS | CLONE_NEWPID | (userNamespacesSupported() ? CLONE_NEWUSER : 0)
            });

            if (pid.wait()) {
                debug("PID namespaces do not work on this system: cannot remount /proc");
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

bool SupplementaryGroup::isConflict(const SupplementaryGroup & other) const
{
    return group == other.group || (gid.has_value() && gid == other.gid);
}

std::string SupplementaryGroup::to_string() const
{
    return nlohmann::json(*this).dump();
}

std::ostream & operator<<(std::ostream & os, const SupplementaryGroups & groups)
{
    return os << nlohmann::json(groups).dump();
}

void to_json(nlohmann::json & j, const SupplementaryGroup & v)
{
    j.emplace("group", v.group);
    if (v.gid.has_value()) j.emplace("gid", v.gid);
}

void from_json(const nlohmann::json & j, SupplementaryGroup & r)
{
    static auto getGroup = [&r](const auto & j) {
        if (j.is_string()) {
            r.group = j.template get<std::string>();
            if (r.group.empty())
                throw Error("supplementary-groups: group must not be empty");
        } else if (j.is_number())
            r.group = fmt("%i", j.template get<uint>());
        else
            throw Error("supplementary-groups: expected string or number: %1%", j.dump());
    };
    if (j.is_object()) {
        if (j.contains("group"))
            getGroup(j.at("group"));
        r.gid = j.value("gid", r.gid);
        r.name = j.value("name", r.name);
    } else
        getGroup(j);
}

}
