#pragma once

#include "types.hh"

namespace nix {

class Store;

void printDotGraph(Store & store, const PathSet & roots);

}
