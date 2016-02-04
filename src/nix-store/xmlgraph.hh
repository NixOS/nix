#pragma once

#include "types.hh"

namespace nix {

class StoreAPI;

void printXmlGraph(StoreAPI & store, const PathSet & roots);

}
