#pragma once


#include <chrono>
#include <optional>

#include "types.hh"

namespace nix {

#if __linux__
std::optional<Path> getCgroupFS();

std::map<std::string, std::string> getCgroups(const Path & cgroupFile);
#endif

struct CgroupStats
{
    std::optional<std::chrono::microseconds> cpuUser, cpuSystem;
};

#if __linux__
/* Destroy the cgroup denoted by 'path'. The postcondition is that
   'path' does not exist, and thus any processes in the cgroup have
   been killed. Also return statistics from the cgroup just before
   destruction. */
CgroupStats destroyCgroup(const Path & cgroup);
#endif

}

