#include "nix/util/current-process.hh"
#include "nix/util/environment-variables.hh"
#include "nix/util/error.hh"
#include "nix/util/logging.hh"

#include "unix/current-process-private.hh"

#include <cmath>

#include <sys/resource.h>

namespace nix {

std::chrono::microseconds getCpuUserTime()
{
    struct rusage buf;

    if (getrusage(RUSAGE_SELF, &buf) != 0) {
        throw SysError("failed to get CPU time");
    }

    std::chrono::seconds seconds(buf.ru_utime.tv_sec);
    std::chrono::microseconds microseconds(buf.ru_utime.tv_usec);

    return seconds + microseconds;
}

size_t unix::savedStackSize = 0;

void ensureStackSizeAtLeast(size_t stackSize)
{
    struct rlimit limit;
    if (getrlimit(RLIMIT_STACK, &limit) == 0 && static_cast<size_t>(limit.rlim_cur) < stackSize) {
        unix::savedStackSize = limit.rlim_cur;
        if (limit.rlim_max < static_cast<rlim_t>(stackSize)) {
            if (getEnv("_NIX_TEST_NO_ENVIRONMENT_WARNINGS") != "1") {
                logger->log(
                    lvlWarn,
                    HintFmt(
                        "Stack size hard limit is %1%, which is less than the desired %2%. If possible, increase the hard limit, e.g. with 'ulimit -Hs %3%'.",
                        limit.rlim_max,
                        stackSize,
                        stackSize / 1024)
                        .str());
            }
        }
        auto requestedSize = std::min(static_cast<rlim_t>(stackSize), limit.rlim_max);
        limit.rlim_cur = requestedSize;
        if (setrlimit(RLIMIT_STACK, &limit) != 0) {
            logger->log(
                lvlError,
                HintFmt(
                    "Failed to increase stack size from %1% to %2% (desired: %3%, maximum allowed: %4%): %5%",
                    unix::savedStackSize,
                    requestedSize,
                    stackSize,
                    limit.rlim_max,
                    std::strerror(errno))
                    .str());
        }
    }
}

} // namespace nix
