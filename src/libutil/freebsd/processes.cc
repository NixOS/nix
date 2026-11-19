#include "nix/util/processes.hh"
#include "nix/util/environment-variables.hh"
#include "nix/util/executable-path.hh"
#include "nix/util/file-system.hh"
#include "nix/util/freebsd-embryo.hh"
#include "nix/util/result.hh"
#include "nix/util/system-error.hh"
#include "nix/util/util.hh"

#include <map>

#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

extern char ** environ;

namespace nix {

/* Configure `embryo` as `options` and `fdr` specify. This only sets it up;
   launching it is the separate step `Embryo::exec()`.

   The embryo blocks no signals and gives every signal its default
   disposition, which is the state `restoreProcessContext()` puts a forked
   child in. */
static freebsd::Embryo &
configureEmbryo(freebsd::Embryo & embryo, const SpawnOptions & options, std::span<const FdRedirection> fdr)
{
    /* A forked child inherits our standard input, output and error unless they
       are redirected; an embryo has no descriptors at all, so give it ours. */
    for (Descriptor fd : {FdRedirection::stdInput, FdRedirection::stdOut, FdRedirection::stdError})
        if (fcntl(fd, F_GETFD) != -1)
            embryo.setFd(fd, fd);

    /* The fork-based implementations apply each redirection in the child, to
       the descriptor table the earlier ones left. A `from` that an earlier
       redirection replaced therefore means what was installed there, not our
       own descriptor of that number. */
    std::map<Descriptor, Descriptor> installed;
    for (const auto & [from, to] : fdr) {
        auto i = installed.find(from);
        Descriptor source = i != installed.end() ? i->second : from;
        embryo.setFd(to, source);
        installed.insert_or_assign(to, source);
    }

    /* Give it our root, and the requested working directory or else ours. */
    auto cwd = options.chdir.value_or(".");
    auto cwdFd = valueOr(
        openDirectoryTraversable(cwd),
        NIX_THROW_NATIVE_SYS_ERROR_HANDLER("opening working directory %s", PathFmt(cwd)));
    embryo.chroot().chdir(cwdFd.get());

    if (options.gid || options.uid) {
        struct setcred cred = SETCRED_INITIALIZER;
        u_int flags = 0;
        if (options.gid) {
            cred.sc_gid = cred.sc_rgid = cred.sc_svgid = *options.gid;
            /* Drop all other groups if we're setgid. */
            cred.sc_supp_groups_nb = 0;
            flags |= SETCREDF_GID | SETCREDF_RGID | SETCREDF_SVGID | SETCREDF_SUPP_GROUPS;
        }
        if (options.uid) {
            cred.sc_uid = cred.sc_ruid = cred.sc_svuid = *options.uid;
            flags |= SETCREDF_UID | SETCREDF_RUID | SETCREDF_SVUID;
        }
        embryo.setCred(flags, cred);
    }

    if (options.setSid)
        embryo.setsid();

    return embryo;
}

/* The program to run: a forked child searches the `PATH` of the environment it
   was given, so do the same. */
static std::filesystem::path resolveProgram(const SpawnOptions & options)
{
    if (!options.lookupPath)
        return options.program;
    auto path = options.environment ? get(*options.environment, OsString{"PATH"}) : nullptr;
    return (path ? ExecutablePath::parse(*path) : ExecutablePath::load()).findPath(options.program);
}

static Strings getArgs(const SpawnOptions & options)
{
    Strings args(options.args);
    /* Allow the caller to specify an alternative argv[0]. Useful for self-exec
       trickery. */
    args.push_front(options.argv0.value_or(options.program.native()));
    return args;
}

static Strings getEnv(const SpawnOptions & options)
{
    Strings env;
    if (options.environment)
        for (auto & [name, value] : *options.environment)
            env.push_back(name + "=" + value);
    else
        for (char ** e = environ; *e; ++e)
            env.push_back(*e);
    return env;
}

/* Rather than forking a child that changes itself into what is wanted, create
   an empty embryonic process, install exactly that in it, and then launch
   it. */
Pid spawnProgram(const SpawnOptions & options, std::span<const FdRedirection> fdr)
{
    freebsd::Embryo embryo(options.dieWithParent);

    try {
        configureEmbryo(embryo, options, fdr);
        return std::move(embryo).exec(resolveProgram(options), getArgs(options), getEnv(options));
    } catch (Error & e) {
        /* No code of ours runs in the child, so the housekeeping errors that
           the fork-based implementations have the child report with exit
           status 127 are thrown here instead. Report them as ExecError all
           the same, as posix_spawn would. */
        auto execErr = ExecError(W_EXITCODE(127, 0), "could not start program %1%", PathFmt(options.program));
        execErr.addTrace({}, HintFmt("%s", e.msg()));
        throw std::move(execErr);
    }
}

} // namespace nix
