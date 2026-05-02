#pragma once

#include "derivation-builder-impl.hh"

namespace nix {

struct FreeBSDDerivationBuilder : virtual DerivationBuilderImpl
{
    using DerivationBuilderImpl::DerivationBuilderImpl;
};

} // namespace nix
