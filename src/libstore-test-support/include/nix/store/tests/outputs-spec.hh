#pragma once
///@file

#include <exception> // IWYU pragma: keep (Needed by rapidcheck on Darwin and FreeBSD)
#include <rapidcheck/gen/Arbitrary.h>

#include "nix/store/outputs-spec.hh"

namespace rc {

template<>
struct Arbitrary<nix::OutputsSpec>
{
    static Gen<nix::OutputsSpec> arbitrary();
};

} // namespace rc
