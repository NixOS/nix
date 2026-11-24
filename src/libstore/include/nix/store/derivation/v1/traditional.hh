#pragma once
///@file

#include "nix/store/derivation/output.hh"
#include "nix/store/derivation/full-inputs.hh"

namespace nix {

struct SingleDerivedPath;

namespace derivation {

template<typename Input, typename Out>
struct Derivation;
using Full = Derivation<SingleDerivedPath, Output>;
using Basic = Derivation<StorePath, Output>;

/**
 * Whether the store directory may contain characters that the ATerm
 * format escapes --- in practice the `\` of a Windows store directory
 * such as `C:\ProgramData\nix\store`.
 *
 * We could always enable it, as escape sequences should only be allowed
 * to be used when needed, but out of an abundance of caution we make
 * it a flag instead.
 */
inline constexpr bool defaultSupportWindowsStoreDir =
#ifdef _WIN32
    true
#else
    false
#endif
    ;

namespace masked {
struct HashInputs;
}

/**
 * The derivation shapes the ATerm format can render: with the inputs
 * already flattened (`FullInputs`), or with them replaced by their
 * masked hashes (`masked::HashInputs`), which is the form whose hash is
 * an input address.
 *
 * @note This is a named concept rather than a `requires` clause written
 * out at each declaration because two atomic constraints are only
 * identical when formed from the *same appearance* of an expression;
 * spelling the same condition twice would declare two distinct
 * overloads.
 */
template<typename Inputs, typename Out>
concept RenderableDerivation =
    // Regular `FullInputs` case takes the `Output` variant, or either of
    // the input-addressing alternatives on their own
    (std::is_same_v<Inputs, FullInputs>
     && (std::is_same_v<Out, Output> || std::is_same_v<Out, Output::InputAddressed>
         || std::is_same_v<Out, Output::Deferred>) )
    // Masked hash is only for input addressing, with masked (`Deferred`) or unmasked (`InputAddressed`) outputs
    || (std::is_same_v<Inputs, masked::HashInputs>
        && (std::is_same_v<Out, Output::InputAddressed> || std::is_same_v<Out, Output::Deferred>) );

/**
 * A derivation in the traditional shape: the one the ATerm format, the
 * wire protocol, and the legacy (version 4) JSON format all share, the
 * name following hnix-store's `TraditionalDerivation`.
 *
 * This corresponds closely to the on-disk `.drv` format: inputs are in
 * one of the format-oriented representations rather than the flat set
 * callers hold, and the environment is kept verbatim --- including the
 * legacy encodings of the various `Derivation` options, and the
 * `__json` encoding of structured attributes.
 *
 * It is a *lossy projection* of `Derivation`: `lower` simply drops the
 * parsed options and structured attributes, keeping only the raw
 * environment. Faithfulness is checked by round-tripping: a
 * `Derivation` is representable in the ATerm format iff
 * `elaborate(lower(drv), ...) == drv`, i.e. iff its parsed fields are
 * in sync with their environment-variable encoding. Derivations whose
 * options are specified directly (e.g. via newer formats like JSON)
 * without the legacy environment encoding are not ATerm-representable.
 *
 * Not every instantiation has an elaborated counterpart: the
 * masked form (`masked::Drv`) names its input derivations by hash, and
 * masks its own outputs and the environment variables named after them,
 * so it is only ever printed and hashed, and read back by the tests.
That is why `lower` and
 * `elaborate` are free functions over the shapes that do have one,
 * rather than members here.
 */
template<typename Inputs = FullInputs, typename Out = Output>
struct TraditionalParsedT
{
    Outputs<Out> outputs;
    Inputs inputs;
    std::string platform;
    std::string builder;
    Strings args;
    /**
     * Verbatim, including any legacy option-encoding variables and the
     * `__json` structured-attributes encoding.
     */
    StringPairs env;

    bool operator==(const TraditionalParsedT &) const = default;

    /**
     * Parse the textual ATerm format.
     *
     * The `masked::HashInputs` cases name their input derivations by
     * hash rather than by store path. Nothing in production reads the
     * masked forms, which exist to be hashed. Being able to read them
     * lets the tests check that reading gives back what was printed,
     * i.e. that printing loses nothing. That no value has a second
     * accepted spelling is a separate property, of the parser: it
     * rejects needless escapes, out-of-order or duplicate keys, and
     * input derivation hashes not in lower-case base-16.
     *
     * @param name The derivation name (not part of the format proper;
     * it comes from the store path / file name), needed to compute
     * fixed-output paths.
     */
    static TraditionalParsedT parse(
        const StoreDirConfig & store,
        std::string && s,
        std::string_view name,
        bool supportWindowsStoreDir = defaultSupportWindowsStoreDir,
        const ExperimentalFeatureSettings & xpSettings = experimentalFeatureSettings)
        requires RenderableDerivation<Inputs, Out>;

    /**
     * Print to the textual ATerm format.
     *
     * @param name The derivation name (not part of the format proper);
     * needed to compute fixed-output paths.
     */
    std::string to_string(
        const StoreDirConfig & store,
        std::string_view name,
        bool supportWindowsStoreDir = defaultSupportWindowsStoreDir) const
        requires RenderableDerivation<Inputs, Out>;
};

using TraditionalParsed = TraditionalParsedT<>;
using BasicTraditionalParsed = TraditionalParsedT<StorePathSet>;

/**
 * Project a `Derivation` down to its ATerm shape.
 *
 * This is lossy: the parsed options are dropped, and the structured
 * attributes are re-encoded as the `__json` environment variable. See
 * `TraditionalParsedT` for how faithfulness is ensured regardless.
 */
TraditionalParsed lower(const Full & drv);
BasicTraditionalParsed lower(const Basic & drv);

/**
 * The inverse of `lower`: parse the legacy environment-variable
 * encodings (structured attributes, derivation options) back into a
 * `Derivation`.
 *
 * @param name The derivation name (not part of the format proper; it
 * comes from the store path / file name).
 */
Full elaborate(
    const TraditionalParsed & aterm,
    const StoreDirConfig & store,
    std::string_view name,
    const ExperimentalFeatureSettings & xpSettings = experimentalFeatureSettings);
Basic elaborate(
    const BasicTraditionalParsed & aterm,
    const StoreDirConfig & store,
    std::string_view name,
    const ExperimentalFeatureSettings & xpSettings = experimentalFeatureSettings);

} // namespace derivation

} // namespace nix

/**
 * The traditional shape in JSON: the same fields, with the inputs and outputs
 * in their own JSON renderings and the environment verbatim. This is
 * not a derivation JSON format of its own --- it has no `name` or
 * `version` --- but the legacy (version 4) format is built on it, and
 * it is what lets the masked forms be read back.
 */
namespace nlohmann {
#define ARG nix::derivation::TraditionalParsedT<Inputs, Out>
template<typename Inputs, typename Out>
JSON_IMPL_WITH_XP_FEATURES_INNER(ARG);
#undef ARG
} // namespace nlohmann
