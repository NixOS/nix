#include "nix/store/build/child.hh"
#include "nix/util/current-process.hh"
#include "nix/util/logging.hh"

#include <fcntl.h>
#include <signal.h>
#include <unistd.h>

namespace nix {

void commonChildInit()
{
    logger = makeSimpleLogger();

    const static std::string pathNullDevice = "/dev/null";
    restoreProcessContext(false);

    /* Reset all signal dispositions to SIG_DFL. The process that started
       the daemon (typically systemd) may have set SIG_IGN for certain
       signals, and ignored signal dispositions survive both fork() and
       execve(). Without this reset, builds are non-deterministic depending
       on how the daemon was started. */
    {
        struct sigaction act = {};
        act.sa_handler = SIG_DFL;
        sigemptyset(&act.sa_mask);
        for (int sig = 1; sig < NSIG; sig++) {
            if (sig == SIGKILL || sig == SIGSTOP)
                continue;
            sigaction(sig, &act, nullptr);
        }
    }

    /* Put the child in a separate session (and thus a separate
       process group) so that it has no controlling terminal (meaning
       that e.g. ssh cannot open /dev/tty) and it doesn't receive
       terminal signals. */
    if (setsid() == -1)
        throw SysError("creating a new session");

    /* Dup stderr to stdout. */
    if (dup2(STDERR_FILENO, STDOUT_FILENO) == -1)
        throw SysError("cannot dup stderr into stdout");

    /* Reroute stdin to /dev/null. */
    int fdDevNull = open(pathNullDevice.c_str(), O_RDWR);
    if (fdDevNull == -1)
        throw SysError("cannot open '%1%'", pathNullDevice);
    if (dup2(fdDevNull, STDIN_FILENO) == -1)
        throw SysError("cannot dup null device into stdin");
    close(fdDevNull);
}

} // namespace nix
