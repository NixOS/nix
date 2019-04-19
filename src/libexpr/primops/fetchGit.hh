#pragma once

#include "store-api.hh"

#include <regex>

namespace nix {

struct GitInfo
{
    Path storePath;
    std::string ref;
    Hash rev{htSHA1};
    std::optional<uint64_t> revCount;
};

GitInfo exportGit(ref<Store> store, const std::string & uri,
    std::optional<std::string> ref,
    std::optional<Hash> rev,
    const std::string & name);

}
