#pragma once
///@file

#include "nix/store-api.hh"

namespace nix {

void printDotGraph(ref<Store> store, StorePathSet && roots);

}
