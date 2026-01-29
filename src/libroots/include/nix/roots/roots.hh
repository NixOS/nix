#pragma once

#include "nix/util/strings.hh"
#include <boost/unordered/unordered_flat_map.hpp>
#include <boost/unordered/unordered_flat_set.hpp>
#include <filesystem>

namespace nix::roots_tracer {

struct TracerConfig
{
    const std::filesystem::path storeDir = "/nix/store";
    const std::filesystem::path stateDir = "/nix/var/nix";
    const std::filesystem::path socketPath = "/nix/var/nix/gc-socket/socket";
};

/**
 * A value of type `Roots` is a mapping from a store path to the set of roots that keep it alive
 */
typedef boost::unordered_flat_map<
    std::string,
    boost::unordered_flat_set<std::string, StringViewHash, std::equal_to<>>,
    StringViewHash,
    std::equal_to<>>
    UncheckedRoots;

void findRuntimeRoots(const TracerConfig & opts, UncheckedRoots & unchecked, bool censor);

void findRoots(const TracerConfig & opts, const Path & path, std::filesystem::file_type type, UncheckedRoots & roots);

} // namespace nix::roots_tracer
