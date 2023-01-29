#pragma once

#include <rapidcheck/gen/Arbitrary.h>

#include <value/context.hh>

namespace rc {
using namespace nix;

template<>
struct Arbitrary<NixStringContextElem> {
    static Gen<NixStringContextElem> arbitrary();
};

}
