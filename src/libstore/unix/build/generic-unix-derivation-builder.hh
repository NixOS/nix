#pragma once
///@file

#include "nix/store/build/derivation-builder.hh"

namespace nix {

struct LocalStore;

DerivationBuilderUnique makeGenericUnixDerivationBuilder(
    LocalStore & store, std::unique_ptr<DerivationBuilderCallbacks> miscMethods, DerivationBuilderParams params);

} // namespace nix
