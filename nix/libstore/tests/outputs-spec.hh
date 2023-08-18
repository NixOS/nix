#pragma once
///@file

#include <rapidcheck/gen/Arbitrary.h>

#include <outputs-spec.hh>

#include <path.hh>

namespace rc {
using namespace nix;

template<>
struct Arbitrary<OutputsSpec> {
    static Gen<OutputsSpec> arbitrary();
};

}
