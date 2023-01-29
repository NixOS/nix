#pragma once

#include <rapidcheck.h>

#include "path.hh"

namespace rc {
using namespace nix;

template<>
struct Arbitrary<StorePath> {
    static Gen<StorePath> arbitrary();
};

}
