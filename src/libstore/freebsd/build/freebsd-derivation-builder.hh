#pragma once

#include "derivation-builder-impl.hh"

namespace nix {

struct FreeBSDDerivationBuilder : virtual DerivationBuilderImpl
{
    using DerivationBuilderImpl::DerivationBuilderImpl;

    FreeBSDDerivationBuilder(FreeBSDDerivationBuilder &&) = delete;
    FreeBSDDerivationBuilder(const FreeBSDDerivationBuilder &) = delete;
    FreeBSDDerivationBuilder & operator=(FreeBSDDerivationBuilder &&) = delete;
    FreeBSDDerivationBuilder & operator=(const FreeBSDDerivationBuilder &) = delete;
    /* To appease Wweak-vtables. */
    ~FreeBSDDerivationBuilder() override;
};

} // namespace nix
