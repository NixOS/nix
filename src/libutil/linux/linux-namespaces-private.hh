#pragma once

#include "nix/util/file-descriptor.hh"

namespace nix {

extern AutoCloseFD fdSavedMountNamespace;
extern AutoCloseFD fdSavedRoot;
extern bool havePrivateMountNs;

} // namespace nix
