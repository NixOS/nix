#include <algorithm>
#include <cstring>

#include "nix/util/current-process.hh"
#include "nix/util/util.hh"
#include "nix/util/finally.hh"
#include "nix/util/file-system.hh"
#include "nix/util/processes.hh"
#include "nix/util/signals.hh"
#include "nix/util/environment-variables.hh"
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
        auto cgroupFS = linux::getCgroupFS();
        if (!cgroupFS)
            return 0;

        auto cpuFile = *cgroupFS / linux::getCurrentCgroup().rel() / "cpu.max";

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
                    savedStackSize,
                    requestedSize,
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

std::optional<std::filesystem::path> getSelfExe()
{
    static auto cached = []() -> std::optional<std::filesystem::path> {
#if defined(__linux__) || defined(__GNU__)
        return readLink(std::filesystem::path{"/proc/self/exe"});
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

        // FreeBSD's sysctl(KERN_PROC_PATHNAME) includes the null terminator in
        // pathLen. Strip it to prevent Nix evaluation errors when the path is
        // serialized to JSON and evaluated as a Nix string.
        path.pop_back();

        return Path(path.begin(), path.end());
#else
        return std::nullopt;
#endif
    }();
    return cached;
}

} // namespace nix
