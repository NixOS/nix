#pragma once
///@file

#include <rapidcheck/gen/Arbitrary.h>

#include "nix/store/derived-path.hh"

#include "nix/store/tests/path.hh"
#include "nix/store/tests/outputs-spec.hh"

namespace rc {

template<>
struct Arbitrary<nix::SingleDerivedPath::Opaque>
{
    static Gen<nix::SingleDerivedPath::Opaque> arbitrary();
};

template<>
struct Arbitrary<nix::SingleDerivedPath::Built>
{
    static Gen<nix::SingleDerivedPath::Built> arbitrary();
};

template<>
struct Arbitrary<nix::SingleDerivedPath>
{
    static Gen<nix::SingleDerivedPath> arbitrary();
};

template<>
struct Arbitrary<nix::DerivedPath::Built>
{
    static Gen<nix::DerivedPath::Built> arbitrary();
};

template<>
struct Arbitrary<nix::DerivedPath>
{
    static Gen<nix::DerivedPath> arbitrary();
};

} // namespace rc
