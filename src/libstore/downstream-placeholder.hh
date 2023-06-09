#pragma once
///@file

#include "hash.hh"
#include "path.hh"

namespace nix {

/**
 * Downstream Placeholders are opaque and almost certainly unique values
 * used to allow derivations to refer to store objects which are yet to
 * be built and for we do not yet have store paths for.
 *
 * They correspond to `DerivedPaths` that are not `DerivedPath::Opaque`,
 * except for the cases involving input addressing or fixed outputs
 * where we do know a store path for the derivation output in advance.
 *
 * Unlike `DerivationPath`, however, `DownstreamPlaceholder` is
 * purposefully opaque and obfuscated. This is so they are hard to
 * create by accident, and so substituting them (once we know what the
 * path to store object is) is unlikely to capture other stuff it
 * shouldn't.
 *
 * We use them with `Derivation`: the `render()` method is called to
 * render an opaque string which can be used in the derivation, and the
 * resolving logic can substitute those strings for store paths when
 * resolving `Derivation.inputDrvs` to `BasicDerivation.inputSrcs`.
 */
class DownstreamPlaceholder
{
    /**
     * `DownstreamPlaceholder` is just a newtype of `Hash`.
     * This its only field.
     */
    Hash hash;

    /**
     * Newtype constructor
     */
    DownstreamPlaceholder(Hash hash) : hash(hash) { }

public:
    /**
     * This creates an opaque and almost certainly unique string
     * deterministically from the placeholder.
     */
    std::string render() const;

    /**
     * Create a placeholder for an unknown output of a content-addressed
     * derivation.
     *
     * The derivation itself is known (we have a store path for it), but
     * the output doesn't yet have a known store path.
     */
    static DownstreamPlaceholder unknownCaOutput(
        const StorePath & drvPath,
        std::string_view outputName);

    /**
     * Create a placehold for the output of an unknown derivation.
     *
     * The derivation is not yet known because it is a dynamic
     * derivaiton --- it is itself an output of another derivation ---
     * and we just have (another) placeholder for it.
     *
     * @param xpSettings Stop-gap to avoid globals during unit tests.
     */
    static DownstreamPlaceholder unknownDerivation(
        const DownstreamPlaceholder & drvPlaceholder,
        std::string_view outputName,
        const ExperimentalFeatureSettings & xpSettings = experimentalFeatureSettings);
};

}
