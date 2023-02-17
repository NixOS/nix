#pragma once

#include <rapidcheck/gen/Arbitrary.h>

#include "nix/store/derived-path.hh"

#include "tests/path.hh"
#include "tests/outputs-spec.hh"

namespace rc {
using namespace nix;

template<>
struct Arbitrary<DerivedPath::Opaque> {
    static Gen<DerivedPath::Opaque> arbitrary();
};

template<>
struct Arbitrary<DerivedPath::Built> {
    static Gen<DerivedPath::Built> arbitrary();
};

template<>
struct Arbitrary<DerivedPath> {
    static Gen<DerivedPath> arbitrary();
};

}
