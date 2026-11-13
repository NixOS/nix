#pragma once
///@file

#include <chrono>
#include <cstdint>
#include <map>
#include <optional>
#include <filesystem>

#include "nix/util/types.hh"
#include "nix/util/canon-path.hh"

namespace nix::linux {

std::optional<std::filesystem::path> getCgroupFS();

StringMap getCgroups(const std::filesystem::path & cgroupFile);

struct CgroupStats
{
    std::optional<std::chrono::microseconds> cpuUser, cpuSystem;

    /**
     * Cgroup statistics keyed by name: `memoryPeak`, `memorySwapPeak`, `ioReadBytes`, `ioWriteBytes`,
     * `oomKilledProcessCount`. A statistic is absent when the kernel lacks that file or can't parse it.
     */
    std::map<std::string, uint64_t> resourceUsageByStatisticName;
};

/**
 * Read statistics from the given cgroup.
 */
CgroupStats getCgroupStats(const std::filesystem::path & cgroup);

/**
 * Destroy the cgroup denoted by 'path'. The postcondition is that
 * 'path' does not exist, and thus any processes in the cgroup have
 * been killed. Also return statistics from the cgroup just before
 * destruction.
 */
CgroupStats destroyCgroup(const std::filesystem::path & cgroup);

CanonPath getCurrentCgroup();

/**
 * Get the cgroup that should be used as the parent when creating new
 * sub-cgroups. The first time this is called, the current cgroup will be
 * returned, and then all subsequent calls will return the original cgroup.
 */
CanonPath getRootCgroup();

/**
 * Make `getRootCgroup()` return `cgroup`. A process that does not start
 * in the root cgroup, such as a daemon worker, calls this before
 * anything calls `getRootCgroup()`. It is not synchronised.
 */
void setRootCgroup(CanonPath cgroup);

} // namespace nix::linux
