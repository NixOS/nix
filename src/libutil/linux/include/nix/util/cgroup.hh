#pragma once
///@file

#include <chrono>
#include <optional>
#include <filesystem>
#include <string_view>

#include "nix/util/types.hh"
#include "nix/util/canon-path.hh"

namespace nix::linux {

std::optional<std::filesystem::path> getCgroupFS();

StringMap getCgroups(const std::filesystem::path & cgroupFile);

struct CgroupStats
{
    std::optional<std::chrono::microseconds> cpuUser, cpuSystem;
};

/**
 * Read statistics from the given cgroup.
 */
CgroupStats getCgroupStats(const std::filesystem::path & cgroup);

/**
 * Given the contents of a cgroup's `cgroup.controllers`, compute the line to
 * write to its `cgroup.subtree_control` that enables every controller we use
 * for per-build accounting (`cpu`, `memory`, `io`, `pids`) that is actually
 * available. Returns `std::nullopt` when none of them are available.
 */
std::optional<std::string> subtreeControlEnableLine(std::string_view availableControllers);

/**
 * Delegate resource controllers to the children of `cgroup` by enabling them
 * in its `cgroup.subtree_control`, so that per-build sub-cgroups expose
 * `memory.peak`, `io.stat` and `memory.events` rather than only the
 * always-present `cpu.stat`. The cgroup must have no member processes (the
 * cgroup-v2 "no internal process" rule), so the caller must first move its own
 * process into a leaf sub-cgroup. Throws on failure.
 */
void delegateCgroupControllers(const std::filesystem::path & cgroup);

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

} // namespace nix::linux
