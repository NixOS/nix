#pragma once
///@file

#ifdef __APPLE__

#  include "nix/store/build/derivation-builder.hh"

namespace nix {

/**
 * Create a derivation builder for Darwin/macOS
 */
std::unique_ptr<DerivationBuilder> makeDarwinDerivationBuilder(
    Store & store,
    std::unique_ptr<DerivationBuilderCallbacks> miscMethods,
    DerivationBuilderParams params,
    bool useSandbox);

} // namespace nix

#endif