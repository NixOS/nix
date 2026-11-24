#pragma once
///@file

#include <rapidcheck/gen/Arbitrary.h>

#include "nix/util/canon-path.hh"

namespace rc {

/**
 * From arbitrary `Filename`s.
 */
template<>
struct Arbitrary<nix::CanonPath>
{
    static Gen<nix::CanonPath> arbitrary();
};

} // namespace rc
