#pragma once
///@file

#include "nix/store/path.hh"
#include "nix/store/derived-path-map.hh"

#include <set>

namespace nix {

struct SingleDerivedPath;

} // namespace nix

namespace nix::derivation {

/**
 * Inputs for full Derivation - both source and derivation inputs
 *
 * This is used for parsing on-disk formats, but then we convert to a set.
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

    /**
     * Convert to a flat set of `SingleDerivedPath`
     */
    std::set<SingleDerivedPath> toSet() const;

    /**
     * Convert from a flat set of `SingleDerivedPath`
     */
    static FullInputs fromSet(const std::set<SingleDerivedPath> & inputs);
};

/**
 * Does the derivation have a dependency on the output of a dynamic
 * derivation?
 *
 * In other words, does it depend on the output of a derivation that is
 * itself an output of a derivation? This corresponds to a dependency
 * that is an inductive derived path with more than one layer of
 * `DerivedPath::Built`.
 */
bool hasDynamicDrvDep(const FullInputs & inputs);

} // namespace nix::derivation
