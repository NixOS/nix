#pragma once

#include "store-api.hh"

#include <regex>

namespace nix {

struct GitInfo
{
    Path storePath;
    std::string ref;
    Hash rev{htSHA1};
    uint64_t revCount;
    time_t lastModified;
};

GitInfo exportGit(ref<Store> store, std::string uri,
    std::optional<std::string> ref,
    std::optional<Hash> rev,
    const std::string & name);

}
