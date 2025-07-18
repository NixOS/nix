#pragma once
///@file

#include <chrono>
#include <optional>

#include "nix/util/types.hh"

namespace nix {

std::optional<Path> getCgroupFS();

StringMap getCgroups(const Path & cgroupFile);

struct CgroupStats
{
    std::optional<std::chrono::microseconds> cpuUser, cpuSystem;
};

/**
 * Destroy the cgroup denoted by 'path'. The postcondition is that
 * 'path' does not exist, and thus any processes in the cgroup have
 * been killed. Also return statistics from the cgroup just before
 * destruction.
 */
CgroupStats destroyCgroup(const Path & cgroup);

std::string getCurrentCgroup();

/**
 * Get the cgroup that should be used as the parent when creating new
 * sub-cgroups. The first time this is called, the current cgroup will be
 * returned, and then all subsequent calls will return the original cgroup.
 */
std::string getRootCgroup();

} // namespace nix
