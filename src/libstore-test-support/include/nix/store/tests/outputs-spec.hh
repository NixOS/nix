#pragma once
///@file

#include <exception> // Needed by rapidcheck on Darwin
#include <rapidcheck/gen/Arbitrary.h>

#include "nix/store/outputs-spec.hh"

#include "nix/store/tests/path.hh"

namespace rc {
using namespace nix;

template<>
struct Arbitrary<OutputsSpec>
{
    static Gen<OutputsSpec> arbitrary();
};

} // namespace rc
