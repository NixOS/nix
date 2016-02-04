#pragma once

#include "types.hh"

namespace nix {

class Store;

void printDotGraph(ref<Store> store, const PathSet & roots);

}
