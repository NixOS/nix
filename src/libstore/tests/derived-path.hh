#pragma once

#include <rapidcheck/gen/Arbitrary.h>

#include <derived-path.hh>

#include "tests/path.hh"
#include "tests/outputs-spec.hh"

namespace rc {
using namespace nix;

template<>
struct Arbitrary<DerivedPath> {
    static Gen<DerivedPath> arbitrary();
};

}
