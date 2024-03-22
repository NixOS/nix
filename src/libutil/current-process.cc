#include <algorithm>
#include <cstring>

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

#if __FreeBSD__
#include <sys/param.h>
#include <sys/sysctl.h>
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
    restoreSignals();
    if (restoreMounts) {
        restoreMountNamespace();
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
        #elif __FreeBSD__
        int mib[4];
        mib[0] = CTL_KERN;
        mib[1] = KERN_PROC;
        mib[2] = KERN_PROC_PATHNAME;
        mib[3] = -1;
        char buf[1024];
        size_t cb = sizeof(buf);
        if(sysctl(mib, 4, buf, &cb, NULL, 0) == -1) {
	        return std::nullopt;
        }
        return Path(buf, cb);
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
