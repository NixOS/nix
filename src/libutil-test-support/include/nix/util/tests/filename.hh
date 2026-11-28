#pragma once
///@file

#include <rapidcheck/gen/Arbitrary.h>

#include "nix/util/filename.hh"

namespace rc {

/**
 * Mostly from a few short names, so that the same ones come up often,
 * but sometimes any valid name.
 */
template<>
struct Arbitrary<nix::Filename>
{
    static Gen<nix::Filename> arbitrary();
};

} // namespace rc
