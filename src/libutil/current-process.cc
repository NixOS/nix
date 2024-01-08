#include "current-process.hh"
#include "namespaces.hh"
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
        auto quota = cpuMaxParts[0];
        auto period = cpuMaxParts[1];
        if (quota != "max")
                return std::ceil(std::stoi(quota) / std::stof(period));
    } catch (Error &) { ignoreException(lvlDebug); }
    #endif

    return 0;
}


//////////////////////////////////////////////////////////////////////


#if __linux__
rlim_t savedStackSize = 0;
#endif

void setStackSize(size_t stackSize)
{
    #if __linux__
    struct rlimit limit;
    if (getrlimit(RLIMIT_STACK, &limit) == 0 && limit.rlim_cur < stackSize) {
        savedStackSize = limit.rlim_cur;
        limit.rlim_cur = stackSize;
        setrlimit(RLIMIT_STACK, &limit);
    }
    #endif
}

void restoreProcessContext(bool restoreMounts)
{
    restoreSignals();
    if (restoreMounts) {
        restoreMountNamespace();
    }

    #if __linux__
    if (savedStackSize) {
        struct rlimit limit;
        if (getrlimit(RLIMIT_STACK, &limit) == 0) {
            limit.rlim_cur = savedStackSize;
            setrlimit(RLIMIT_STACK, &limit);
        }
    }
    #endif
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
