#pragma once
#include "nix/store/build/derivation-builder.hh"

namespace nix {
struct LocalStore;
#ifdef __FreeBSD__
DerivationBuilderUnique makeFreeBSDChrootDerivationBuilder(
    LocalStore & store, std::unique_ptr<DerivationBuilderCallbacks> miscMethods, DerivationBuilderParams params);
DerivationBuilderUnique makeFreeBSDDerivationBuilder(
    LocalStore & store, std::unique_ptr<DerivationBuilderCallbacks> miscMethods, DerivationBuilderParams params);
#endif
} // namespace nix
