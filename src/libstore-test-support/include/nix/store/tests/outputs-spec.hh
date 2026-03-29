#pragma once
///@file

#include <exception> // Needed by rapidcheck on Darwin
#include <rapidcheck/gen/Arbitrary.h>

#include "nix/store/outputs-spec.hh"

#include "nix/store/tests/path.hh"

namespace rc {

template<>
struct Arbitrary<nix::OutputsSpec>
{
    static Gen<nix::OutputsSpec> arbitrary();
};

} // namespace rc
