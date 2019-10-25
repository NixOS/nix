#pragma once

#include "types.hh"

namespace nix {

class Store;

void printGraphML(ref<Store> store, const PathSet & roots);

}
