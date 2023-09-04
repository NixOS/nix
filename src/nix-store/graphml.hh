#pragma once
///@file

#include "store-api.hh"

namespace nix {

void printGraphML(ref<Store> store, StorePathSet && roots);

}
