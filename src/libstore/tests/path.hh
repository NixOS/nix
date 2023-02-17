#pragma once

#include <rapidcheck/gen/Arbitrary.h>

#include "nix/store/path.hh"

namespace nix {

struct StorePathName {
    std::string name;
};

}

namespace rc {
using namespace nix;

template<>
struct Arbitrary<StorePathName> {
    static Gen<StorePathName> arbitrary();
};

template<>
struct Arbitrary<StorePath> {
    static Gen<StorePath> arbitrary();
};

}
