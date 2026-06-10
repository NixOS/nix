#include "nix/util/current-process.hh"
#include "nix/util/environment-variables.hh"
#include "nix/util/executable-path.hh"
#include "nix/util/fmt.hh"
#include "nix/util/signals.hh"
#include "nix/util/processes.hh"
#include "nix/util/finally.hh"
#include "nix/util/serialise.hh"

#include <cerrno>
#include <filesystem>
#include <cstdlib>
#include <cstring>
#include <future>
#include <iostream>
#include <atomic>
using namespace std::chrono_literals;

#include <grp.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#ifdef __APPLE__
#  include <sys/syscall.h>
#endif

#ifdef __linux__
#  include <sys/prctl.h>
#  include <sys/mman.h>
#endif

#include "util-unix-config-private.hh"

namespace nix {

Pid::Pid() {}

Pid::Pid(Pid && other) noexcept
    : pid(other.pid)
    , separatePG(other.separatePG)
    , killSignal(other.killSignal)
{
    other.release();
}

Pid::Pid(pid_t pid)
    : pid(pid)
{
}

Pid::~Pid()
try {
    if (pid != -1)
        kill(/*allowInterrupts=*/false);
} catch (...) {
    ignoreExceptionInDestructor();
}

void Pid::operator=(pid_t pid)
{
    if (this->pid != -1 && this->pid != pid)
        kill();
    this->pid = pid;
    killSignal = SIGKILL; // reset signal to default
}

Pid::operator pid_t()
{
    return pid;
}

int Pid::kill(bool allowInterrupts)
{
    assert(pid != -1);

    debug("killing process %1%", pid);

    std::atomic<bool> killed = false;

    if (killTimeout > 0ms && killSignal != SIGKILL)
        killThread = std::thread([&]() {
            auto elapsed = 0ms;
            while (elapsed < killTimeout) {
                std::this_thread::sleep_for(25ms);
                elapsed += 25ms;
                if (killed)
                    return;
            }
            ::kill(separatePG ? -pid : pid, SIGKILL);
        });

    /* Send the requested signal to the child.  If it has its own
       process group, send the signal to every process in the child
       process group (which hopefully includes *all* its children). */
    if (::kill(separatePG ? -pid : pid, killSignal) != 0) {
        /* On BSDs, killing a process group will return EPERM if all
           processes in the group are zombies (or something like
           that). So try to detect and ignore that situation. */
#if defined(__FreeBSD__) || defined(__APPLE__)
        if (errno != EPERM || ::kill(pid, 0) != 0)
#endif
            logError(SysError("killing process %d", pid).info());
    }

    int ret = wait(allowInterrupts);
    if (killThread.joinable()) {
        killed = true;
        killThread.join();
    }
    return ret;
}

int Pid::wait(bool allowInterrupts)
{
    assert(pid != -1);
    while (1) {
        int status;
        int res = waitpid(pid, &status, 0);
        if (res == pid) {
            pid = -1;
            return status;
        }
        if (errno != EINTR)
            throw SysError("cannot get exit status of PID %d", pid);
        if (allowInterrupts)
            checkInterrupt();
    }
}

void Pid::setSeparatePG(bool separatePG)
{
    this->separatePG = separatePG;
}

void Pid::setKillSignal(int signal)
{
    this->killSignal = signal;
}

void Pid::setKillTimeout(std::chrono::milliseconds duration)
{
    this->killTimeout = duration;
}

pid_t Pid::release()
{
    pid_t p = pid;
    /* We use the move assignment operator rather than setting the individual fields so we aren't duplicating the
       default values from the header, which would be hard to keep in sync. If we just used the assignment operator
       without manually resetting pid first it would kill that process, however, so we do manually reset that one field.
     */
    pid = -1;
    *this = Pid();
    return p;
}

pid_t Pid::get()
{
    return pid;
}

void killUser(uid_t uid)
{
    debug("killing all processes running under uid '%1%'", uid);

    assert(uid != 0); /* just to be safe... */

    /* The system call kill(-1, sig) sends the signal `sig' to all
       users to which the current process can send signals.  So we
       fork a process, switch to uid, and send a mass kill. */

    Pid pid = startProcess([&] {
        if (setuid(uid) == -1)
            throw SysError("setting uid");

        while (true) {
#ifdef __APPLE__
            /* OSX's kill syscall takes a third parameter that, among
               other things, determines if kill(-1, signo) affects the
               calling process. In the OSX libc, it's set to true,
               which means "follow POSIX", which we don't want here
                 */
            if (syscall(SYS_kill, -1, SIGKILL, false) == 0)
                break;
#else
            if (kill(-1, SIGKILL) == 0)
                break;
#endif
            if (errno == ESRCH || errno == EPERM)
                break; /* no more processes */
            if (errno != EINTR)
                throw SysError("cannot kill processes for uid '%1%'", uid);
        }

        _exit(0);
    });

    int status = pid.wait();
    if (status != 0)
        throw Error("cannot kill processes for uid '%1%': %2%", uid, statusToString(status));

    /* !!! We should really do some check to make sure that there are
       no processes left running under `uid', but there is no portable
       way to do so (I think).  The most reliable way may be `ps -eo
       uid | grep -q $uid'. */
}

//////////////////////////////////////////////////////////////////////

using ChildWrapperFunction = fun<void()>;

/* Wrapper around vfork to prevent the child process from clobbering
   the caller's stack frame in the parent. */
static pid_t doFork(bool allowVfork, ChildWrapperFunction & fun) __attribute__((noinline));

static pid_t doFork(bool allowVfork, ChildWrapperFunction & fun)
{
#ifdef __linux__
    pid_t pid = allowVfork ? vfork() : fork();
#else
    pid_t pid = fork();
#endif
    if (pid != 0)
        return pid;
    fun();
    unreachable();
}

#ifdef __linux__
static int childEntry(void * arg)
{
    auto & fun = *reinterpret_cast<ChildWrapperFunction *>(arg);
    fun();
    return 1;
}
#endif

pid_t startProcess(fun<void()> processMain, const ProcessOptions & options)
{
    auto newLogger = makeSimpleLogger().release();
    ChildWrapperFunction wrapper = [&] {
        if (!options.allowVfork) {
            /* Set a simple logger, while leaking (not destroying)
               the parent logger. We don't want to run the parent
               logger's destructor since that will crash (e.g. when
               ~ProgressBar() tries to join a thread that doesn't
               exist. */
            logger = newLogger;
        }
        try {
#ifdef __linux__
            if (options.dieWithParent && prctl(PR_SET_PDEATHSIG, SIGKILL) == -1)
                throw SysError("setting death signal");
#endif
            processMain();
        } catch (std::exception & e) {
            try {
                std::cerr << options.errorPrefix << e.what() << "\n";
            } catch (...) {
            }
        } catch (...) {
        }
        if (options.runExitHandlers)
            exit(1);
        else
            _exit(1);
    };

    pid_t pid = -1;

    if (options.cloneFlags) {
#ifdef __linux__
        // Not supported, since then we don't know when to free the stack.
        assert(!(options.cloneFlags & CLONE_VM));

        size_t stackSize = 1 * 1024 * 1024;
        auto stack = static_cast<char *>(
            mmap(0, stackSize, PROT_WRITE | PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0));
        if (stack == MAP_FAILED)
            throw SysError("allocating stack");

        Finally freeStack([&] { munmap(stack, stackSize); });

        pid = clone(childEntry, stack + stackSize, options.cloneFlags | SIGCHLD, &wrapper);
#else
        throw Error("clone flags are only supported on Linux");
#endif
    } else
        pid = doFork(options.allowVfork, wrapper);

    if (pid == -1)
        throw SysError("unable to fork");

    return pid;
}

std::string runProgram(std::filesystem::path program, bool lookupPath, const OsStrings & args, bool isInteractive)
{
    auto res = runProgram(
        RunOptions{
            .program = program,
            .lookupPath = lookupPath,
            .args = args,
            .isInteractive = isInteractive,
        });

    if (!statusOk(res.first))
        throw ExecError(res.first, "program %s %s", PathFmt(program), statusToString(res.first));

    return res.second;
}

void runProgram2(const RunOptions & options)
{
    checkInterrupt();

    /* Create a pipe. */
    auto out = std::make_shared<Pipe>();
    if (options.standardOut)
        out->create();

    auto pid = startProgram(options, out);

    out->writeSide.close();

    if (options.standardOut)
        drainFD(out->readSide.get(), *options.standardOut);

    /* Wait for the child to finish. */
    int status = pid.wait();
    if (status)
        throw ExecError(status, "program %1% %2%", PathFmt(options.program), statusToString(status));
}

Pid startProgram(const RunOptions & options, std::shared_ptr<Pipe> out)
{
    ProcessOptions processOptions;
    // vfork implies that the environment of the main process and the fork will
    // be shared (technically this is undefined, but in practice that's the
    // case), so we can't use it if we alter the environment
    processOptions.allowVfork = !options.environment;

    auto suspension = logger->suspendIf(options.isInteractive);

    return startProcess(
        [&] {
            if (options.environment)
                replaceEnv(*options.environment);
            if (options.standardOut && dup2(out->writeSide.get(), STDOUT_FILENO) == -1)
                throw SysError("dupping stdout");
            if (options.mergeStderrToStdout)
                if (dup2(STDOUT_FILENO, STDERR_FILENO) == -1)
                    throw SysError("cannot dup stdout into stderr");
            for (auto redirection : options.redirections) {
                if (dup2(redirection.to, redirection.from) == -1) {
                    throw SysError("dupping fd %i to %i", redirection.from, redirection.to);
                }
            }

            if (options.chdir && chdir((*options.chdir).c_str()) == -1)
                throw SysError("chdir failed");
#ifdef __linux__
            if (!options.caps.empty() && prctl(PR_SET_KEEPCAPS, 1) < 0) {
                throw SysError("setting keep-caps failed");
            }
#endif
            if (options.gid && setgid(*options.gid) == -1)
                throw SysError("setgid failed");
            /* Drop all other groups if we're setgid. */
            if (options.gid && setgroups(0, 0) == -1)
                throw SysError("setgroups failed");
            if (options.uid && setuid(*options.uid) == -1)
                throw SysError("setuid failed");

#ifdef __linux__
            if (!options.caps.empty()) {
                if (prctl(PR_SET_KEEPCAPS, 0)) {
                    throw SysError("clearing keep-caps failed");
                }

                // we do the capability dance like this to avoid a dependency
                // on libcap, which has a rather large build closure and many
                // more features that we need for now. maybe some other time.
                static constexpr uint32_t LINUX_CAPABILITY_VERSION_3 = 0x20080522;
                static constexpr uint32_t LINUX_CAPABILITY_U32S_3 = 2;
                struct user_cap_header_struct
                {
                    uint32_t version;
                    int pid;
                } hdr = {LINUX_CAPABILITY_VERSION_3, 0};
                struct user_cap_data_struct
                {
                    uint32_t effective;
                    uint32_t permitted;
                    uint32_t inheritable;
                } data[LINUX_CAPABILITY_U32S_3] = {};
                for (auto cap : options.caps) {
                    assert(cap / 32 < LINUX_CAPABILITY_U32S_3);
                    data[cap / 32].permitted |= 1 << (cap % 32);
                    data[cap / 32].inheritable |= 1 << (cap % 32);
                }
                if (syscall(SYS_capset, &hdr, data)) {
                    throw SysError("couldn't set capabilities");
                }

                for (auto cap : options.caps) {
                    if (prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_RAISE, cap, 0, 0) < 0) {
                        throw SysError("couldn't set ambient caps");
                    }
                }
            }
#endif

            Strings args_(options.args);
            args_.push_front(options.program.native());

            restoreProcessContext();

            if (options.lookupPath)
                execvp(options.program.c_str(), stringsToCharPtrs(args_).data());
            // This allows you to refer to a program with a pathname relative
            // to the PATH variable.
            else
                execv(options.program.c_str(), stringsToCharPtrs(args_).data());

            throw SysError("executing %s", PathFmt(options.program));
        },
        processOptions);
}

//////////////////////////////////////////////////////////////////////

std::string statusToString(int status)
{
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        if (WIFEXITED(status))
            return fmt("failed with exit code %1%", WEXITSTATUS(status));
        else if (WIFSIGNALED(status)) {
            int sig = WTERMSIG(status);
#if HAVE_STRSIGNAL
            const char * description = strsignal(sig);
            return fmt("failed due to signal %1% (%2%)", sig, description);
#else
            return fmt("failed due to signal %1%", sig);
#endif
        } else
            return "died abnormally";
    } else
        return "succeeded";
}

bool statusOk(int status)
{
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

int execvpe(const char * file0, const char * const argv[], const char * const envp[])
{
    auto file = ExecutablePath::load().findPath(file0);
    // `const_cast` is safe. See the note in
    // https://pubs.opengroup.org/onlinepubs/9799919799/functions/exec.html
    return execve(file.c_str(), const_cast<char * const *>(argv), const_cast<char * const *>(envp));
}

} // namespace nix
