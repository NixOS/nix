#pragma once

#include <string>

#include "util.hh"

namespace nix {

class Store;

struct GitInfo
{
    Path storePath;
    std::string rev;
    std::string shortRev;
    uint64_t revCount = 0;
};

GitInfo exportGit(ref<Store> store, const std::string & uri,
    std::experimental::optional<std::string> ref = {},
    const std::string & rev = "",
    const std::string & name = "");

}
