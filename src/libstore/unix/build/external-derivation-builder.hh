#pragma once
///@file

#include "nix/store/build/derivation-builder.hh"

namespace nix {

struct LocalStore;

DerivationBuilderUnique makeExternalDerivationBuilder(
    LocalStore & store,
    std::unique_ptr<DerivationBuilderCallbacks> miscMethods,
    DerivationBuilderParams params,
    const ExternalBuilder & handler);

} // namespace nix
