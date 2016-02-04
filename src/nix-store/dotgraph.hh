#pragma once

#include "types.hh"

namespace nix {

class StoreAPI;

void printDotGraph(StoreAPI & store, const PathSet & roots);

}
