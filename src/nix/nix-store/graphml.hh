#pragma once
///@file

#include "nix/store/store-api.hh"

namespace nix {

void printGraphML(ref<Store> store, StorePathSet && roots);

}
