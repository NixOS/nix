#pragma once
///@file

#include <rapidcheck/gen/Arbitrary.h>

#include "nix/outputs-spec.hh"

#include "nix/tests/path.hh"

namespace rc {
using namespace nix;

template<>
struct Arbitrary<OutputsSpec> {
    static Gen<OutputsSpec> arbitrary();
};

}
