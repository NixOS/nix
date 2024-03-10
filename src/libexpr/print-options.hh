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

    /**
     * Indentation width for pretty-printing.
     *
     * If set to 0 (the default), values are not pretty-printed.
     */
    size_t prettyIndent = 0;

    /**
     * True if pretty-printing is enabled.
     */
    inline bool shouldPrettyPrint()
    {
        return prettyIndent > 0;
    }
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

/**
 * `PrintOptions` for unknown and therefore potentially large values in
 * debugging contexts, to avoid printing "too much" output.
 *
 * This is like `errorPrintOptions`, but prints more values.
 */
static PrintOptions debugPrintOptions = PrintOptions {
    .ansiColors = true,
    .force = true,
    .derivationPaths = true,
    .maxDepth = 15,
    .maxAttrs = 32,
    .maxListItems = 32,
    .maxStringLength = 1024,
    .prettyIndent = 2
};

}
