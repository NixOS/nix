#pragma once
#ifdef __APPLE__
#include "nix/store/build/derivation-builder.hh"

namespace nix {
class LocalStore;
DerivationBuilderUnique makeDarwinDerivationBuilder(
    LocalStore & store,
    std::unique_ptr<DerivationBuilderCallbacks> miscMethods,
    DerivationBuilderParams params,
    bool useSandbox);
} // namespace nix
#endif
