#pragma once

#include "unix-derivation-builder-impl.hh"

namespace nix {

struct FreeBSDDerivationBuilder : virtual UnixDerivationBuilderImpl
{
    using UnixDerivationBuilderImpl::UnixDerivationBuilderImpl;

    FreeBSDDerivationBuilder(FreeBSDDerivationBuilder &&) = delete;
    FreeBSDDerivationBuilder(const FreeBSDDerivationBuilder &) = delete;
    FreeBSDDerivationBuilder & operator=(FreeBSDDerivationBuilder &&) = delete;
    FreeBSDDerivationBuilder & operator=(const FreeBSDDerivationBuilder &) = delete;
    /* To appease Wweak-vtables. */
    ~FreeBSDDerivationBuilder() override;
};

} // namespace nix
