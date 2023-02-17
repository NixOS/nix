#pragma once

#include <rapidcheck/gen/Arbitrary.h>

#include "nix/store/outputs-spec.hh"

#include "tests/path.hh"

namespace rc {
using namespace nix;

template<>
struct Arbitrary<OutputsSpec> {
    static Gen<OutputsSpec> arbitrary();
};

}
