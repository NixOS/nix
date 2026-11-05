#pragma once
///@file

#include "nix/store/store-api.hh"
#include "nix/store/path.hh"
#include "nix/util/ref.hh"

namespace nix {

void printGraphML(ref<Store> store, StorePathSet && roots);

}
