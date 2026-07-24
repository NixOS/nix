#include "nix/util/processes.hh"
#include "nix/util/current-process.hh"
#include "nix/util/file-descriptor.hh"
#include "nix/util/environment-variables.hh"
#include "nix/util/signals.hh"
#include "nix/util/util.hh"
#include "nix/util/serialise.hh"

#include "linux/linux-namespaces-private.hh"
#include "unix/signals-private.hh"
#include "unix/current-process-private.hh"
#include "util-unix-config-private.hh"

#include <cstring>
#include <cerrno>

#include <sys/resource.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/syscall.h>
#include <sys/prctl.h>
#include <grp.h>
#include <unistd.h>
#include <limits.h>
#include <sched.h>

extern char ** environ __attribute__((weak));

namespace nix {

namespace {

/* This structure intentionally doesn't have any fancy classes from C++ like
   std::optional, because I don't trust those. Only plain pointers or integral
   types. */
struct ExecChildParams
{
    const char * program;
    const char * chdir;
    char * const * environment;
    char * const * args;
    bool mergeStderrToStdout;
    bool lookupPath;
    Descriptor stdoutFd;
    Descriptor errorPipe;
    bool setGid;
    gid_t gid;
    bool setUid;
    uid_t uid;
    bool dieWithParent;
};

/*
 * This code is supposed to run in the child right after vfork(). We never
 * return from this function through normal means, but rather always _exit() or
 * execv[e] from it.
 *
 * We shouldn't allow any exceptions to escape and shouldn't allocate any memory,
 * or in general do anything that's not async-signal-safe.
 *
 * There's a slight wrinkle in that in a multithreaded program on Linux, only
 * the thread calling vfork() is suspended while other threads of the parent
 * continue running.
 *
 * This also presents a problem with signals, since those will also arrive in
 * the child, which shares the address space. Thankfully, we don't have many
 * signal handlers that would be negatively affected by this, so we punt on
 * the issue.
 *
 * Another thing worth mentioning is that apparently, WSL and QEMU userspace
 * emulation both implement vfork() as plain fork() so the regular footguns of
 * fork() apply.
 *
 * Don't call any functions defined in other translations units from here!
 * (for auditability purposes and avoiding accidentally calling something you
 * shouldn't).
 *
 * Even calling libc functions is quite sketchy in general because of lazy
 * binding with dynamic linking. Whatever the libc is doing in the dynamic
 * linker is likely to not really be safe with vfork().
 *
 * Luckily, nixpkgs now builds with eager binding (-z now) so our builds won't
 * be affected. TODO: Just build libutil with -z now too, instead of relying on
 * hardening defaults.
 */
[[gnu::noinline, noreturn]] static void doExecChild(const ExecChildParams & params) noexcept
{
    auto die = [&params] [[noreturn]] (int err, const char * msg) {
        [[maybe_unused]] ssize_t ret; /* Swallow all errors. */
        ret = ::write(params.errorPipe, reinterpret_cast<const char *>(&err), sizeof(err));
        ret = ::write(params.errorPipe, msg, ::strlen(msg));
        ::_exit(1);
    };

    auto dieWithErrno = [&die] [[noreturn]] (const char * msg) { die(errno, msg); };

    /* TODO: Do something with stdin if we don't want to keep it? */

    if (params.stdoutFd != INVALID_DESCRIPTOR && ::dup2(params.stdoutFd, STDOUT_FILENO) == -1)
        dieWithErrno("dupping stdout");

    if (params.mergeStderrToStdout && ::dup2(STDOUT_FILENO, STDERR_FILENO) == -1)
        dieWithErrno("cannot dup stdout into stderr");

    if (params.chdir && ::chdir(params.chdir) == -1)
        dieWithErrno("chdir failed");

    /* Restore saved process context. Much like nix::restoreProcessContext, but inlined
       and without any possibility of throwing exceptions. */

    if (havePrivateMountNs) {
        char savedCwd[PATH_MAX];

        /* On Linux, it seems like cwd can't ever be larger than PATH_MAX (as
           restricted by the syscall itself). Once again, intentionally not
           calling into libc. */
        if (::syscall(SYS_getcwd, savedCwd, sizeof(savedCwd)) == -1)
            dieWithErrno("getcwd failed");

        if (::setns(fdSavedMountNamespace.get(), CLONE_NEWNS) == -1)
            dieWithErrno("restoring parent mount namespace");

        if (fdSavedRoot) {
            if (::fchdir(fdSavedRoot.get()))
                dieWithErrno("chdir into saved root");

            if (::chroot("."))
                dieWithErrno("chroot into saved root");
        }

        if (::chdir(savedCwd) == -1)
            dieWithErrno("restoring cwd");
    }

    /* Important! Calling syscalls directly and not libc functions because of a
       discrepancy in POSIX specification (i.e. POSIX setgid/setuid has to apply
       to all threads, while the syscall only applies to a task).
       A huge footgun is apparently the fact that glibc defines SYS_setgid to the 16 bit
       version of the syscall, while musl "fixes it up" to use the 32 bit version.
       The end result is that we have to make sure to use the right one still. */

#ifdef SYS_setuid32
#  define NIX_SYS_setuid SYS_setuid32
#  define NIX_SYS_setgid SYS_setgid32
#  define NIX_SYS_setgroups SYS_setgroups32
#else
#  define NIX_SYS_setuid SYS_setuid
#  define NIX_SYS_setgid SYS_setgid
#  define NIX_SYS_setgroups SYS_setgroups
#endif

    if (params.setGid && ::syscall(NIX_SYS_setgid, params.gid) == -1)
        dieWithErrno("setgid failed");

    /* Drop all other groups if we're setgid. */
    if (params.setGid && ::syscall(NIX_SYS_setgroups, 0, 0) == -1)
        dieWithErrno("setgroups failed");

    if (params.setUid && ::syscall(NIX_SYS_setuid, params.uid) == -1)
        dieWithErrno("setuid failed");

    /* Technically slightly racy, we might want to do something like what
       preserveDeathSignal does. */
    if (params.dieWithParent && prctl(PR_SET_PDEATHSIG, SIGKILL) == -1)
        dieWithErrno("setting death signal");

#undef NIX_SYS_setuid
#undef NIX_SYS_setgid
#undef NIX_SYS_setgroups

    if (unix::savedStackSize) {
        struct ::rlimit limit;
        if (::getrlimit(RLIMIT_STACK, &limit) == 0) {
            limit.rlim_cur = unix::savedStackSize;
            ::setrlimit(RLIMIT_STACK, &limit);
            /* TODO: Why do we ignore all errors here? */
        }
    }

    /* Like unix::restoreSignals(), but safe to do in a vfork child. */
    if (unix::savedSignalMaskIsSet && sigprocmask(SIG_SETMASK, &unix::savedSignalMask, nullptr) == -1)
        dieWithErrno("restoring signals");

    /* TODO: Close leaked file descriptors? The best way is with close_range(). */

    if (params.lookupPath)
        /* Nonstandard, but both musl and glibc have it and it doesn't
           seem to do anything weird or allocate memory, so it should
           be fine-ish? The use of execvp has some footguns though (see
           https://github.com/NixOS/nix/pull/9494) and maybe we should get rid
           of it entirely and do executable path resolution in the parent.
           The hacky ENOEXEC handling also doesn't seem to exist in musl.

           This does path lookup in the global environment of the parent, and
           not in the params.environment like it's done in runProgram2 for other
           unixes (arguably a bugfix)!

           Don't accidentally call nix::execvpe! */
        ::execvpe(params.program, params.args, params.environment);
    else
        ::execve(params.program, params.args, params.environment);

    dieWithErrno("could not exec program");
}

[[gnu::noinline]] static pid_t startExecChildInVFork(const ExecChildParams & params) noexcept
{
    pid_t pid = ::vfork();

    /* In the unlikely scenario that vfork() fails we return -1 here too. */
    if (pid != 0)
        return pid;

    /* Now run the actual child. */
    doExecChild(params);
}

/* TODO: This can be factored out if this becomes useful for other platforms? */
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

} // namespace

/* TODO: Factor this out into a `launchProgram` that returns a pid. That would be
   much more useful in more places. */
void runProgram2(const RunOptions & options)
{
    checkInterrupt();

    /* Create a pipe. */
    Pipe out;
    if (options.standardOut)
        out.create();

    /* Pipe that the child reports errors through. */
    Pipe childErrorPipe;
    childErrorPipe.create();

    /* Prepare arguments and environment for the child. */
    Strings args_(options.args);
    args_.push_front(options.program.native());
    const Strings env_ = options.environment ? prepareEnvironmentStrings(*options.environment) : Strings{};
    const auto env = stringsToCharPtrs(env_);
    const auto args = stringsToCharPtrs(args_);

    const ExecChildParams params = {
        .program = options.program.c_str(),
        .chdir = options.chdir ? options.chdir->c_str() : nullptr,
        .environment = options.environment ? env.data() : environ,
        .args = args.data(),
        .mergeStderrToStdout = options.mergeStderrToStdout,
        .lookupPath = options.lookupPath,
        .stdoutFd = options.standardOut ? out.writeSide.get() : INVALID_DESCRIPTOR,
        .errorPipe = childErrorPipe.writeSide.get(),
        .setGid = options.gid.has_value(),
        /* The default is not used, but a bit sketchy to leave zero initialised so "nobody". */
        .gid = options.gid.value_or(65534),
        .setUid = options.uid.has_value(),
        /* The default is not used, but a bit sketchy to leave zero initialised so "nobody". */
        .uid = options.uid.value_or(65534),
        .dieWithParent = true, /* TODO: Maybe we might want to expose this in RunOptions? */
    };

    auto suspension = logger->suspendIf(options.isInteractive);

    const auto savedErrno = errno;

    /* Fork. */
    Pid pid = startExecChildInVFork(params);

    /* errno is also shared with the child, so it can trample it. Restoring it
       doesn't necessarily matter much though. */
    const auto forkErrno = errno;
    errno = savedErrno;

    if (pid == -1)
        throw SysError(forkErrno, "unable to vfork");

    out.writeSide.close();
    childErrorPipe.writeSide.close();

    StringSink childErrorSink;
    drainFD(childErrorPipe.readSide.get(), childErrorSink);

    /* We don't write anything to the pipe on success. */
    if (const auto & errorContent = childErrorSink.s; errorContent.size()) {
        int status = pid.wait();

        auto execErr = ExecError(status, "could not start program %1%", PathFmt(options.program));

        /* The child returned garbage through the error pipe. I think it's
           pretty unlikely that this will happen, but maybe a signal arriving
           could result in ::write failing with EINTR. It's unclear to me
           whether that that can happen with writes under PIPE_BUF? */
        if (errorContent.size() >= sizeof(int)) {
            auto intBytes = errorContent.substr(0, sizeof(int));
            int errNo;
            std::memcpy(&errNo, intBytes.data(), sizeof(errNo));
            auto errMsg = errorContent.substr(sizeof(int));
            /* It's a bit strange, but the interface of runProgram2 reports
               spawn helper errors as ExecError. */
            execErr.addTrace({}, HintFmt("spawn helper process failed: %1%: %2%", errMsg, ::strerror(errNo)));
        }

        throw std::move(execErr);
    }

    if (options.standardOut)
        drainFD(out.readSide.get(), *options.standardOut);

    /* Wait for the child to finish. */
    int status = pid.wait();
    if (status)
        throw ExecError(status, "program %1% %2%", PathFmt(options.program), statusToString(status));
}

} // namespace nix
