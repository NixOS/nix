#pragma once
///@file

#include "nix/util/serialise.hh"
#include "nix/store/store-api.hh"

namespace nix::daemon {

enum RecursiveFlag : bool { NotRecursive = false, Recursive = true };

void processConnection(ref<Store> store, FdSource && from, FdSink && to, TrustedFlag trusted, RecursiveFlag recursive);

} // namespace nix::daemon
