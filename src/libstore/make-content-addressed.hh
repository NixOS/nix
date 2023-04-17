#pragma once
///@file

#include "store-api.hh"

namespace nix {

std::map<StorePath, StorePath> makeContentAddressed(
    Store & srcStore,
    Store & dstStore,
    const StorePathSet & storePaths);

}
