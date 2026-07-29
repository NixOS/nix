#pragma once
///@file

#include "nix/store/path.hh"
#include "nix/store/derived-path-map.hh"

#include <set>

namespace nix {

/**
 * Inputs for full Derivation - both source and derivation inputs
 */
struct FullInputs
{
    /**
     * inputs that are sources
     */
    StorePathSet srcs;
    /**
     * inputs that are sub-derivations
     */
    DerivedPathMap<std::set<OutputName, std::less<>>> drvs;

    bool operator==(const FullInputs &) const = default;
};

} // namespace nix
