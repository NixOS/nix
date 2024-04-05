#include <algorithm>
#include <cstring>

#include "current-process.hh"
#include "util.hh"
#include "finally.hh"
#include "file-system.hh"
#include "processes.hh"
#include "signals.hh"

#ifdef __APPLE__
# include <mach-o/dyld.h>
#endif

#if __linux__
# include <mutex>
# include <sys/resource.h>
# include "cgroup.hh"
# include "namespaces.hh"
#endif

#include <sys/mount.h>

namespace nix {

unsigned int getMaxCPU()
{
    #if __linux__
    try {
        auto cgroupFS = getCgroupFS();
        if (!cgroupFS) return 0;

        auto cgroups = getCgroups("/proc/self/cgroup");
        auto cgroup = cgroups[""];
        if (cgroup == "") return 0;

        auto cpuFile = *cgroupFS + "/" + cgroup + "/cpu.max";

        auto cpuMax = readFile(cpuFile);
        auto cpuMaxParts = tokenizeString<std::vector<std::string>>(cpuMax, " \n");

        if (cpuMaxParts.size() != 2) {
            return 0;
        }

        auto quota = cpuMaxParts[0];
        auto period = cpuMaxParts[1];
        if (quota != "max")
                return std::ceil(std::stoi(quota) / std::stof(period));
    } catch (Error &) { ignoreException(lvlDebug); }
    #endif

    return 0;
}


//////////////////////////////////////////////////////////////////////


rlim_t savedStackSize = 0;

void setStackSize(rlim_t stackSize)
{
    struct rlimit limit;
    if (getrlimit(RLIMIT_STACK, &limit) == 0 && limit.rlim_cur < stackSize) {
        savedStackSize = limit.rlim_cur;
        limit.rlim_cur = std::min(stackSize, limit.rlim_max);
        if (setrlimit(RLIMIT_STACK, &limit) != 0) {
            logger->log(
                lvlError,
                HintFmt(
                    "Failed to increase stack size from %1% to %2% (maximum allowed stack size: %3%): %4%",
                    savedStackSize,
                    stackSize,
                    limit.rlim_max,
                    std::strerror(errno)
                ).str()
            );
        }
    }
}

void restoreProcessContext(bool restoreMounts)
{
    unix::restoreSignals();
    if (restoreMounts) {
        #if __linux__
        restoreMountNamespace();
        #endif
    }

    if (savedStackSize) {
        struct rlimit limit;
        if (getrlimit(RLIMIT_STACK, &limit) == 0) {
            limit.rlim_cur = savedStackSize;
            setrlimit(RLIMIT_STACK, &limit);
        }
    }
}


//////////////////////////////////////////////////////////////////////


std::optional<Path> getSelfExe()
{
    static auto cached = []() -> std::optional<Path>
    {
        #if __linux__
        return readLink("/proc/self/exe");
        #elif __APPLE__
        char buf[1024];
        uint32_t size = sizeof(buf);
        if (_NSGetExecutablePath(buf, &size) == 0)
            return buf;
        else
            return std::nullopt;
        #else
        return std::nullopt;
        #endif
    }();
    return cached;
}

}
