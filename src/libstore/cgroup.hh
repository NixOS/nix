#pragma once

#if __linux__

#include "types.hh"

namespace nix {

std::map<std::string, std::string> getCgroups(const Path & cgroupFile);

void destroyCgroup(const Path & cgroup);

}

#endif
