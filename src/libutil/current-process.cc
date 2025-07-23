#include <algorithm>
#include <cstring>

#include "nix/util/current-process.hh"
#include "nix/util/util.hh"
#include "nix/util/finally.hh"
#include "nix/util/file-system.hh"
#include "nix/util/processes.hh"
#include "nix/util/signals.hh"
#include <math.h>

#ifdef __APPLE__
#  include <mach-o/dyld.h>
#endif

#ifdef __linux__
#  include <mutex>
#  include "nix/util/cgroup.hh"
#  include "nix/util/linux-namespaces.hh"
#endif

#ifdef __FreeBSD__
#  include <sys/param.h>
#  include <sys/sysctl.h>
#endif

namespace nix {

unsigned int getMaxCPU()
{
#ifdef __linux__
    try {
        auto cgroupFS = getCgroupFS();
        if (!cgroupFS)
            return 0;

        auto cpuFile = *cgroupFS + "/" + getCurrentCgroup() + "/cpu.max";

        auto cpuMax = readFile(cpuFile);
        auto cpuMaxParts = tokenizeString<std::vector<std::string>>(cpuMax, " \n");

        if (cpuMaxParts.size() != 2) {
            return 0;
        }

        auto quota = cpuMaxParts[0];
        auto period = cpuMaxParts[1];
        if (quota != "max")
            return std::ceil(std::stoi(quota) / std::stof(period));
    } catch (Error &) {
        ignoreExceptionInDestructor(lvlDebug);
    }
#endif

    return 0;
}

//////////////////////////////////////////////////////////////////////

#ifndef _WIN32
size_t savedStackSize = 0;

void setStackSize(size_t stackSize)
{
    struct rlimit limit;
    if (getrlimit(RLIMIT_STACK, &limit) == 0 && static_cast<size_t>(limit.rlim_cur) < stackSize) {
        savedStackSize = limit.rlim_cur;
        limit.rlim_cur = std::min(static_cast<rlim_t>(stackSize), limit.rlim_max);
        if (setrlimit(RLIMIT_STACK, &limit) != 0) {
            logger->log(
                lvlError,
                HintFmt(
                    "Failed to increase stack size from %1% to %2% (maximum allowed stack size: %3%): %4%",
                    savedStackSize,
                    stackSize,
                    limit.rlim_max,
                    std::strerror(errno))
                    .str());
        }
    }
}
#endif

void restoreProcessContext(bool restoreMounts)
{
#ifndef _WIN32
    unix::restoreSignals();
#endif
    if (restoreMounts) {
#ifdef __linux__
        restoreMountNamespace();
#endif
    }

#ifndef _WIN32
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
    static auto cached = []() -> std::optional<Path> {
#if defined(__linux__) || defined(__GNU__)
        return readLink("/proc/self/exe");
#elif defined(__APPLE__)
        char buf[1024];
        uint32_t size = sizeof(buf);
        if (_NSGetExecutablePath(buf, &size) == 0)
            return buf;
        else
            return std::nullopt;
#elif defined(__FreeBSD__)
        int sysctlName[] = {
            CTL_KERN,
            KERN_PROC,
            KERN_PROC_PATHNAME,
            -1,
        };
        size_t pathLen = 0;
        if (sysctl(sysctlName, sizeof(sysctlName) / sizeof(sysctlName[0]), nullptr, &pathLen, nullptr, 0) < 0) {
            return std::nullopt;
        }

        std::vector<char> path(pathLen);
        if (sysctl(sysctlName, sizeof(sysctlName) / sizeof(sysctlName[0]), path.data(), &pathLen, nullptr, 0) < 0) {
            return std::nullopt;
        }

        return Path(path.begin(), path.end());
#else
        return std::nullopt;
#endif
    }();
    return cached;
}

} // namespace nix
