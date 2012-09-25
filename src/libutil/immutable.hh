#pragma once

#include <types.hh>

namespace nix {

/* Make the given path mutable. */
void makeMutable(const Path & path);

}
