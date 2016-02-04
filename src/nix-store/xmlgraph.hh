#pragma once

#include "types.hh"

namespace nix {

class Store;

void printXmlGraph(Store & store, const PathSet & roots);

}
