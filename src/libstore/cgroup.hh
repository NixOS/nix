#pragma once

#if __linux__

#include "types.hh"

namespace nix {

void destroyCgroup(const Path & cgroup);

}

#endif
