#pragma once
///@file

#include <rapidcheck/gen/Arbitrary.h>

#include "nix/expr/value/context.hh"
#include "nix/store/tests/derived-path.hh" // IWYU pragma: keep

namespace rc {

template<>
struct Arbitrary<nix::NixStringContextElem::DrvDeep>
{
    static Gen<nix::NixStringContextElem::DrvDeep> arbitrary();
};

template<>
struct Arbitrary<nix::NixStringContextElem>
{
    static Gen<nix::NixStringContextElem> arbitrary();
};

} // namespace rc
