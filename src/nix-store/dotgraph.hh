#pragma once

#include "nix/store/store-api.hh"

namespace nix {

void printDotGraph(ref<Store> store, StorePathSet && roots);

}
