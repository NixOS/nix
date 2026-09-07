#pragma once

#include "nix/store/store-dir-config.hh"
#include "nix/util/types.hh"

#include <boost/regex.hpp>
#include <boost/unordered/unordered_flat_map.hpp>
#include <boost/unordered/unordered_flat_set.hpp>

namespace nix {

/**
 * The key is a string because std::filesystem::path cannot be used here with
 * macOS's libc++.
 */
using UncheckedRoots = boost::unordered_flat_map<
    std::string,
    boost::unordered_flat_set<std::string, StringViewHash, std::equal_to<>>,
    StringViewHash,
    std::equal_to<>>;

inline boost::regex makeStorePathRegex(const StoreDirConfig & config)
{
    static auto specialRegex = boost::regex(R"([.^$\\*+?()\[\]{}|])");
    auto quotedStoreDir = boost::regex_replace(config.storeDir, specialRegex, R"(\\$&)");
    return boost::regex(quotedStoreDir + R"(/[0-9a-z]+[0-9a-zA-Z\+\-\._\?=]*)");
}

void findDarwinRuntimeRoots(const StoreDirConfig & config, UncheckedRoots & unchecked);

} // namespace nix
