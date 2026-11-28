#pragma once
///@file

#include <rapidcheck/gen/Arbitrary.h>

#include "nix/util/unresolved-path.hh"

#include <string>
#include <vector>

namespace nix::testing {

/**
 * A path as a user might write it, from a few components, so that `.`,
 * `..`, empty components (i.e. `//`), and a leading `/` or not, are
 * all common.
 *
 * @param names The other components to use.
 */
rc::Gen<std::string> rawPath(std::vector<std::string> names);

} // namespace nix::testing

namespace rc {

/**
 * From arbitrary `Filename`s and `..`s.
 */
template<>
struct Arbitrary<nix::UnresolvedPath>
{
    static Gen<nix::UnresolvedPath> arbitrary();
};

} // namespace rc
