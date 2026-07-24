#pragma once
///@file

#include "nix/util/serialise.hh"
#include "nix/store/store-api.hh"

namespace nix {

struct Builder;

namespace daemon {

enum struct RecursiveFlag {
    NotRecursive = 0,
    Recursive = 1,
    RecursiveSubmitted = 2,
};

void processConnection(
    ref<Store> store,
    FdSource && from,
    FdSink && to,
    TrustedFlag trusted,
    RecursiveFlag recursive,
    std::shared_ptr<Builder> builder = nullptr);

} // namespace daemon

} // namespace nix
