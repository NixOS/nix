#pragma once

#include "types.hh"

namespace nix {

class Store;

void printXmlGraph(ref<Store> store, const PathSet & roots);

}
