#pragma once
///@file

#include <rapidcheck/gen/Arbitrary.h>

#include "nix/store/derived-path.hh"

#include "nix/store/tests/path.hh"
#include "nix/store/tests/outputs-spec.hh"

namespace rc {
using namespace nix;

template<>
struct Arbitrary<SingleDerivedPath::Opaque>
{
    static Gen<SingleDerivedPath::Opaque> arbitrary();
};

template<>
struct Arbitrary<SingleDerivedPath::Built>
{
    static Gen<SingleDerivedPath::Built> arbitrary();
};

template<>
struct Arbitrary<SingleDerivedPath>
{
    static Gen<SingleDerivedPath> arbitrary();
};

template<>
struct Arbitrary<DerivedPath::Built>
{
    static Gen<DerivedPath::Built> arbitrary();
};

template<>
struct Arbitrary<DerivedPath>
{
    static Gen<DerivedPath> arbitrary();
};

} // namespace rc
