#pragma once
///@file

#include "nix/store/path.hh"
#include "nix/util/ref.hh"

namespace nix {

class Store;

void printGraphML(ref<Store> store, StorePathSet && roots);

} // namespace nix
