#pragma once
///@file

#include <chrono>
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
 * RAII helper to automatically destroy a cgroup on scope exit.
 */
class AutoDestroyCgroup
{
    std::filesystem::path cgroupPath;

public:
    AutoDestroyCgroup() = default;

    AutoDestroyCgroup(std::filesystem::path path);

    AutoDestroyCgroup(const AutoDestroyCgroup &) = delete;
    AutoDestroyCgroup & operator=(const AutoDestroyCgroup &) = delete;

    AutoDestroyCgroup(AutoDestroyCgroup && other) noexcept
        : cgroupPath(std::move(other.cgroupPath))
    {}

    AutoDestroyCgroup & operator=(AutoDestroyCgroup && other) noexcept
    {
        swap(*this, other);
        return *this;
    }

    friend void swap(AutoDestroyCgroup & lhs, AutoDestroyCgroup & rhs) noexcept
    {
        using std::swap;
        swap(lhs.cgroupPath, rhs.cgroupPath);
    }

    ~AutoDestroyCgroup() noexcept;

    /**
     * Destroy the cgroup now and return statistics.
     * After calling this, the destructor won't do anything.
     */
    CgroupStats destroy();

    /**
     * Cancel the automatic destruction.
     */
    void cancel() noexcept { cgroupPath.clear(); }

    /**
     * Reset to empty state (equivalent to assigning a default-constructed object).
     */
    void reset() noexcept { *this = AutoDestroyCgroup(); }

    /**
     * Get the cgroup path.
     */
    const std::filesystem::path & path() const { return cgroupPath; }

    /**
     * Check if this will destroy a cgroup.
     */
    explicit operator bool() const { return !cgroupPath.empty(); }
};

} // namespace nix::linux
