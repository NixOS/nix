#pragma once

#include "store-api.hh"

#include <regex>

namespace nix {

struct GitInfo
{
    Path storePath;
    std::optional<std::string> ref;
    Hash rev{htSHA1};
    std::optional<uint64_t> revCount;
    time_t lastModified;
};

GitInfo exportGit(
    ref<Store> store,
    std::string uri,
    std::optional<std::string> ref,
    std::optional<Hash> rev,
    const std::string & name);

GitInfo exportGitHub(
    ref<Store> store,
    const std::string & owner,
    const std::string & repo,
    std::optional<std::string> ref,
    std::optional<Hash> rev);

}
