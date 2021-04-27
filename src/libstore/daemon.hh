#pragma once

#include "serialise.hh"
#include "store-api.hh"

namespace nix::daemon {

enum TrustedFlag : bool { NotTrusted = false, Trusted = true };
enum RecursiveFlag : bool { NotRecursive = false, Recursive = true };

void processConnection(
    ref<Store> store,
    FdSource & from,
    FdSink & to,
    TrustedFlag trusted,
    RecursiveFlag recursive,
    /* Arbitrary hook to check authorization / initialize user data / whatever
       after the protocol has been negotiated. The idea is that this function
       and everything it calls doesn't know about this stuff, and the
       `nix-daemon` handles that instead. */
    std::function<void(Store &)> authHook);

}
