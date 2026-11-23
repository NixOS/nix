#pragma once
///@file

#include "nix/store/derivation/traditional.hh"

namespace nix {

struct Source;
struct Sink;

namespace derivation {

/**
 * Print a derivation.
 *
 * Throws if the derivation is not ATerm-representable, i.e. if its
 * first-class options are not in sync with their legacy
 * environment-variable encoding. @see TraditionalParsedT.
 */
std::string
unparse(const Full & drv, const StoreDirConfig & store, bool supportWindowsStoreDir = defaultSupportWindowsStoreDir);

/**
 * Read a derivation from a file.
 */
Full parse(
    const StoreDirConfig & store,
    std::string && s,
    std::string_view name,
    bool supportWindowsStoreDir = defaultSupportWindowsStoreDir,
    const ExperimentalFeatureSettings & xpSettings = experimentalFeatureSettings);

/**
 * Wire protocol serialization of basic derivations, which also uses the
 * legacy ATerm-style representation.
 */
Source & read(Source & in, const StoreDirConfig & store, BasicTraditionalParsed & drv, std::string_view name);
/**
 * @param name The derivation name (not part of the format proper);
 * needed to compute fixed-output paths.
 */
void write(Sink & out, const StoreDirConfig & store, const BasicTraditionalParsed & drv, std::string_view name);

} // namespace derivation

} // namespace nix
