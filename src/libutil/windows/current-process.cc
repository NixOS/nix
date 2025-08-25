#include "nix/util/current-process.hh"
#include "nix/util/windows-error.hh"
#include <cmath>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>

namespace nix {

std::chrono::microseconds getCpuUserTime()
{
    FILETIME creationTime;
    FILETIME exitTime;
    FILETIME kernelTime;
    FILETIME userTime;

    if (!GetProcessTimes(GetCurrentProcess(), &creationTime, &exitTime, &kernelTime, &userTime)) {
        auto lastError = GetLastError();
        throw windows::WinError(lastError, "failed to get CPU time");
    }

    ULARGE_INTEGER uLargeInt;
    uLargeInt.LowPart = userTime.dwLowDateTime;
    uLargeInt.HighPart = userTime.dwHighDateTime;

    // FILETIME stores units of 100 nanoseconds.
    // Dividing by 10 gives microseconds.
    std::chrono::microseconds microseconds(uLargeInt.QuadPart / 10);

    return microseconds;
}

} // namespace nix
#endif // ifdef _WIN32
