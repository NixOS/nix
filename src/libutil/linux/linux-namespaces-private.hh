#pragma once

namespace nix {

class AutoCloseFD;

extern AutoCloseFD fdSavedMountNamespace;
extern AutoCloseFD fdSavedRoot;
extern bool havePrivateMountNs;

} // namespace nix
