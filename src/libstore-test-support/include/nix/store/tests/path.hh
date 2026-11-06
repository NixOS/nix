#pragma once
///@file

#include <rapidcheck/gen/Arbitrary.h>
#include <ostream>
#include <string>

namespace nix {

class StorePath;

struct StorePathName
{
    std::string name;
};

// For rapidcheck
void showValue(const StorePath & p, std::ostream & os);

} // namespace nix

namespace rc {

template<>
struct Arbitrary<nix::StorePathName>
{
    static Gen<nix::StorePathName> arbitrary();
};

template<>
struct Arbitrary<nix::StorePath>
{
    static Gen<nix::StorePath> arbitrary();
};

} // namespace rc
