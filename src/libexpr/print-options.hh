#pragma once
/**
 * @file
 * @brief Options for printing Nix values.
 */

#include <limits>

namespace nix {

/**
 * Options for printing Nix values.
 */
struct PrintOptions
{
    /**
     * If true, output ANSI color sequences.
     */
    bool ansiColors = false;
    /**
     * If true, force values.
     */
    bool force = false;
    /**
     * If true and `force` is set, print derivations as
     * `«derivation /nix/store/...»` instead of as attribute sets.
     */
    bool derivationPaths = false;
    /**
     * If true, track which values have been printed and skip them on
     * subsequent encounters. Useful for self-referential values.
     */
    bool trackRepeated = true;
    /**
     * Maximum depth to evaluate to.
     */
    size_t maxDepth = std::numeric_limits<size_t>::max();
    /**
     * Maximum number of attributes in attribute sets to print.
     *
     * Note that this is a limit for the entire print invocation, not for each
     * attribute set encountered.
     */
    size_t maxAttrs = std::numeric_limits<size_t>::max();
    /**
     * Maximum number of list items to print.
     *
     * Note that this is a limit for the entire print invocation, not for each
     * list encountered.
     */
    size_t maxListItems = std::numeric_limits<size_t>::max();
    /**
     * Maximum string length to print.
     */
    size_t maxStringLength = std::numeric_limits<size_t>::max();
};

/**
 * `PrintOptions` for unknown and therefore potentially large values in error messages,
 * to avoid printing "too much" output.
 */
static PrintOptions errorPrintOptions = PrintOptions {
    .ansiColors = true,
    .maxDepth = 10,
    .maxAttrs = 10,
    .maxListItems = 10,
    .maxStringLength = 1024
};

}
