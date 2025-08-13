#include "nix/util/current-process.hh"
#include <cmath>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>

namespace nix {

float getCpuUserTime()
{
	FILETIME usertime;

    if (!GetProcessTimes(GetCurrentProcess(), NULL, NULL, NULL, &usertime)) {
        return std::nan("cpuUserTime");
    }

    // FILETIME stores units of 100 nanoseconds.
    // Dividing by 10 gives microseconds, then by 1,000,000 gives seconds.
    // So getting from 100 nanoseconds to seconds is dividing by 10,000,000.
    return (float)li.QuadPart / 1e7;
}

} // namespace nix
#endif // ifdef _WIN32
