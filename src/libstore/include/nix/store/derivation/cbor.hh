#pragma once

#include "nix/store/derivations.hh"

namespace nix::derivation {

inline constexpr unsigned expectedCborVersion = 1;

/**
 * Deterministic CBOR interchange encoding. Builder, arguments, and both
 * environment names and values are byte strings. Structured attributes
 * retain their original JSON bytes. See protocols/derivation-cbor.md.
 */
std::string toCbor(const Full & drv);

/** Encode a collection, with store-path base names as text map keys. */
std::string toCbor(const std::map<StorePath, Full> & drvs);

/**
 * Read a single derivation, accepting nonminimal encodings and unordered
 * maps and sets. Call the normal derivation validation before storing it.
 */
Full parseCbor(std::string_view bytes, const ExperimentalFeatureSettings & xpSettings = experimentalFeatureSettings);

} // namespace nix::derivation
