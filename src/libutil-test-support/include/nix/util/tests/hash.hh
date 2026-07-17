#pragma once
///@file

#include <rapidcheck/gen/Arbitrary.h>

#include "nix/util/hash.hh"

namespace rc {

template<>
struct Arbitrary<nix::Hash>
{
    static Gen<nix::Hash> arbitrary();
};

} // namespace rc
