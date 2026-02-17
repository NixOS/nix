#pragma once
#include "nix/store/build/derivation-builder.hh"

namespace nix {
struct LocalStore;
#ifdef __linux__
DerivationBuilderUnique makeLinuxDerivationBuilder(
    LocalStore & store, std::unique_ptr<DerivationBuilderCallbacks> miscMethods, DerivationBuilderParams params);
#endif
} // namespace nix
