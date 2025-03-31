#pragma once
///@file

#include "nix/serialise.hh"
#include "nix/store-api.hh"

namespace nix::daemon {

enum RecursiveFlag : bool { NotRecursive = false, Recursive = true };

void processConnection(
    ref<Store> store,
    FdSource && from,
    FdSink && to,
    TrustedFlag trusted,
    RecursiveFlag recursive);

}
