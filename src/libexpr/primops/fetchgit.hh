#pragma once

#include <string>

#include "ref.hh"

namespace nix {

class Store;

Path exportGit(ref<Store> store,
    const std::string & uri, const std::string & rev);

}
