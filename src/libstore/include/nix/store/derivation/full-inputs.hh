#pragma once
///@file

#include "nix/store/path.hh"
#include "nix/store/derived-path-map.hh"

#include <set>

namespace nix {

struct SingleDerivedPath;

/**
 * Inputs for full Derivation - both source and derivation inputs
 *
 * This is used for parsing on-disk formats, but then we convert to a set.
 */
struct FullInputs
{
    using value_type = StorePath;

    /**
     * inputs that are sources
     */
    StorePathSet srcs;
    /**
     * inputs that are sub-derivations
     */
    DerivedPathMap<std::set<OutputName, std::less<>>> drvs;

    bool operator==(const FullInputs &) const = default;

    /**
     * Convert to a flat set of `SingleDerivedPath`
     */
    std::set<SingleDerivedPath> toSet() const;

    /**
     * Convert from a flat set of `SingleDerivedPath`
     */
    static FullInputs fromSet(const std::set<SingleDerivedPath> & inputs);
};

} // namespace nix
