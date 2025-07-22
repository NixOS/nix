#pragma once
///@file

#include <rapidcheck/gen/Arbitrary.h>

#include "nix/store/path.hh"

namespace nix {

struct StorePathName
{
    std::string name;
};

// For rapidcheck
void showValue(const StorePath & p, std::ostream & os);

} // namespace nix

namespace rc {
using namespace nix;

template<>
struct Arbitrary<StorePathName>
{
    static Gen<StorePathName> arbitrary();
};

template<>
struct Arbitrary<StorePath>
{
    static Gen<StorePath> arbitrary();
};

} // namespace rc
