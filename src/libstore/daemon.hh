#pragma once
///@file

#include "globals.hh"
#include "serialise.hh"
#include "store-api.hh"

namespace nix::daemon {

enum RecursiveFlag : bool { NotRecursive = false, Recursive = true };

void processConnection(
    ref<Store> store,
    FdSource & from,
    FdSink & to,
    AuthenticatedUser user,
    RecursiveFlag recursive);

}
