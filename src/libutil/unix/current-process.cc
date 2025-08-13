#include "nix/util/current-process.hh"
#include <cmath>

#include <sys/resource.h>

namespace nix {

float getCpuUserTime()
{
    struct rusage buf;

    if (getrusage(RUSAGE_SELF, &buf) != 0) {
        return std::nan("cpuUserTime");
    }

    return buf.ru_utime.tv_sec + ((float) buf.ru_utime.tv_usec / 1e6);
}

} // namespace nix
