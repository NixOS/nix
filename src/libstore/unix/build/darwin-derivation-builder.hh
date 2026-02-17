#pragma once
#include "nix/store/build/derivation-builder.hh"

namespace nix {
struct LocalStore;
#ifdef __APPLE__
DerivationBuilderUnique makeDarwinDerivationBuilder(
    LocalStore & store,
    std::unique_ptr<DerivationBuilderCallbacks> miscMethods,
    DerivationBuilderParams params,
    bool useSandbox);
#endif
} // namespace nix
