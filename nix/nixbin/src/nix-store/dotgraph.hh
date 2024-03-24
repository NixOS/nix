#pragma once
///@file

#include "store-api.hh"

namespace nix {

void printDotGraph(ref<Store> store, StorePathSet && roots);

}
