#include "nix/util/processes.hh"
#include "nix/util/util.hh"
#include "nix/util/system-error.hh"
#include "nix/util/finally.hh"

#include "unix/signals-private.hh"

#include <cstring>
#include <cerrno>

#include <sys/syscall.h>
#include <spawn.h>

extern "C" char ** environ __attribute__((weak));

/* These declarations live in spawn_private.h and are available since macOS 10.15.
   After all, why not? */

extern "C" int posix_spawnattr_set_uid_np(const posix_spawnattr_t *, uid_t);
extern "C" int posix_spawnattr_set_gid_np(const posix_spawnattr_t *, gid_t);
extern "C" int posix_spawnattr_set_groups_np(const posix_spawnattr_t *, int, gid_t *, uid_t);

namespace nix {

static Strings prepareEnvironmentStrings(const StringMap & environment)
{
    Strings env;
    for (auto & [name, value] : environment) {
        std::string var;
        var.reserve(name.size() + value.size() + 1);
        var += name;
        var += "=";
        var += value;
        env.push_back(std::move(var));
    }
    return env;
}

Pid spawnProgram(const SpawnOptions & options, std::span<const FdRedirection> fdr)
{
    using namespace unix;

    /* Prepare arguments and environment for the child. */
    Strings args_(options.args);
    /* Allow the caller to specify an alternative argv[0]. Useful for self-exec
       trickery. */
    args_.push_front(options.argv0.value_or(options.program.native()));
    const Strings env_ = options.environment ? prepareEnvironmentStrings(*options.environment) : Strings{};
    const auto env = stringsToCharPtrs(env_);
    const auto args = stringsToCharPtrs(args_);

    /* Remember, posix_spawn_* functions don't set errno but return the error instead. */

    ::posix_spawnattr_t attr = nullptr;
    ::posix_spawn_file_actions_t actions = nullptr;

    if (int err = ::posix_spawnattr_init(&attr))
        throw SysError(err, "posix_spawnattr_init");

    Finally cleanupAttr([&attr] { ::posix_spawnattr_destroy(&attr); });

    if (int err = ::posix_spawn_file_actions_init(&actions))
        throw SysError(err, "posix_spawn_file_actions_init");

    Finally cleanupActions([&actions] { ::posix_spawn_file_actions_destroy(&actions); });

    /* Don't inherit anything unless asked for. */
    short flags = POSIX_SPAWN_CLOEXEC_DEFAULT;

    /* Set the sigmask set below in the spawnattr. */
    if (savedSignalMaskIsSet)
        flags |= POSIX_SPAWN_SETSIGMASK;

    if (options.setSid)
        flags |= POSIX_SPAWN_SETSID;

    if (int err = ::posix_spawnattr_setflags(&attr, flags))
        throw SysError(err, "posix_spawnattr_setflags");

    /* These are always inherited. */
    for (const auto fd : {STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO})
        if (int err = ::posix_spawn_file_actions_addinherit_np(&actions, fd))
            throw SysError(err, "posix_spawn_file_actions_addinherit_np fd %d", fd);

    for (const auto & [from, to] : fdr) {
        if (from == to) {
            /* This case should have been handled in a POSIX defect report, but
               at least according to dup2 macOS manpages it doesn't handle this case. */
            if (int err = ::posix_spawn_file_actions_addinherit_np(&actions, from))
                throw SysError(err, "posix_spawn_file_actions_addinherit_np fd %d", from);
        } else if (int err = ::posix_spawn_file_actions_adddup2(&actions, from, to))
            throw SysError(err, "posix_spawn_file_actions_adddup2 fd %d -> %d", from, to);
    }

    if (options.chdir) {
        if (int err = ::posix_spawn_file_actions_addchdir_np(&actions, options.chdir->c_str()))
            throw SysError(err, "posix_spawn_file_actions_addchdir_np");
    }

    /* This part is absolutely cursed, but XNU does have this functionality... */

    if (options.gid) {
        /* Drop all other groups if we're setgid. The first entry is the egid of the process. */
        gid_t groups[1] = {*options.gid};
        if (int err = ::posix_spawnattr_set_groups_np(&attr, 1, groups, /*KAUTH_UID_NONE=*/(~(uid_t) 0 - 100)))
            throw SysError(err, "posix_spawnattr_set_groups_np");
        if (int err = ::posix_spawnattr_set_gid_np(&attr, *options.gid))
            throw SysError(err, "posix_spawnattr_set_gid_np");
    }

    if (options.uid) {
        if (int err = ::posix_spawnattr_set_uid_np(&attr, *options.uid))
            throw SysError(err, "posix_spawnattr_set_uid_np");
    }

    if (savedSignalMaskIsSet) {
        if (int err = ::posix_spawnattr_setsigmask(&attr, &savedSignalMask))
            throw SysError(err, "posix_spawnattr_setsigmask");
    }

    pid_t pid = INVALID_PID;

    auto * doSpawn = options.lookupPath ? ::posix_spawnp : ::posix_spawn;

    if (int err = doSpawn(
            &pid, options.program.c_str(), &actions, &attr, args.data(), options.environment ? env.data() : environ)) {
        if (err == ENOMEM || err == EAGAIN) /* Internal fork failed. */
            throw SysError(err, "posix_spawn failed");
        auto execErr = ExecError(W_EXITCODE(127, 0), "could not start program %1%", PathFmt(options.program));
        /* Housekeeping errors are reported as ExecError. */
        execErr.addTrace({}, HintFmt("posix_spawn failed: %s", ::strerror(err)));
        throw std::move(execErr);
    }

    assert(pid != INVALID_PID);

    return pid;
}

} // namespace nix
