#pragma once

#include "store-api.hh"

namespace nix {

void printDotGraph(ref<Store> store, StorePathSet && roots);

}
