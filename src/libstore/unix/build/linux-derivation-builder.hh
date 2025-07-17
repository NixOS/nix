#pragma once
///@file

#ifdef __linux__

#  include "nix/store/build/derivation-builder.hh"

namespace nix {

/**
 * Create a derivation builder for Linux
 */
std::unique_ptr<DerivationBuilder> makeLinuxDerivationBuilder(
    Store & store, std::unique_ptr<DerivationBuilderCallbacks> miscMethods, DerivationBuilderParams params);

/**
 * Create a sandboxed derivation builder for Linux with chroot
 */
std::unique_ptr<DerivationBuilder> makeChrootLinuxDerivationBuilder(
    Store & store, std::unique_ptr<DerivationBuilderCallbacks> miscMethods, DerivationBuilderParams params);

} // namespace nix

#endif