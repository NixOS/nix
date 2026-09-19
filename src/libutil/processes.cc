#include "nix/util/processes.hh"
#include "nix/util/serialise.hh"

#ifndef _WIN32
#  include "unix/processes-private.hh"
#  include <unistd.h>
#endif

namespace nix {

void ExecError::anchor() {}

#ifndef _WIN32

void unix::validateRedirections(const RunOptions & options)
{
    /* The child has one stdout. `standardOut` wants to collect it into a sink,
       `standardOutFd` wants it wired to a descriptor the caller already owns,
       and honouring either one silently would discard the other's intent. */
    if (options.standardOut && options.standardOutFd)
        throw UsageError("'standardOut' and 'standardOutFd' are mutually exclusive; the child has only one stdout");

    for (auto redirection : options.redirections) {
        if (redirection.targetFd <= STDERR_FILENO)
            throw UsageError(
                "redirection target fd %i is a standard stream; use 'standardOut' or 'mergeStderrToStdout' instead",
                redirection.targetFd);

#  ifdef __linux__
        /* Kept in step with `relocatedErrorPipeFD` in `linux/processes.cc`. The
           vfork child moves its error pipe there before touching anything else,
           so that number is unusable as either end of a redirection: as a target
           it would overwrite the pipe, and as a source it would itself be
           overwritten by the relocation. */
        if (redirection.targetFd == STDERR_FILENO + 1 || redirection.sourceFd == STDERR_FILENO + 1)
            throw UsageError(
                "fd %i cannot take part in a redirection; it is reserved for the child's error-reporting pipe",
                STDERR_FILENO + 1);
#  endif

        for (auto other : options.redirections)
            if (other.sourceFd == redirection.targetFd)
                throw UsageError(
                    "fd %i is both a redirection target and the source of another redirection, "
                    "which the in-order duplication would clobber",
                    redirection.targetFd);
    }
}

#endif

Pid & Pid::operator=(Pid && other) noexcept
{
    swap(*this, other);
    return *this;
}

std::pair<int, std::string> runProgram(RunOptions && options)
{
    StringSink sink;
    options.standardOut = &sink;

    int status = 0;

    try {
        runProgram2(options);
    } catch (ExecError & e) {
        status = e.status;
    }

    return {status, std::move(sink.s)};
}

} // namespace nix
