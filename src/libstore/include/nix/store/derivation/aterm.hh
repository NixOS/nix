#pragma once
///@file

#include "nix/store/derivation/output.hh"
#include "nix/store/derivation/full-inputs.hh"

namespace nix {

struct Source;
struct Sink;
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
 * hashes modulo (`masked::HashInputs`), which is the form whose hash is
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
    // Hash modulo is only for input addressing, with masked (`Deferred`) or unmasked (`InputAddressed`) outputs
    || (std::is_same_v<Inputs, masked::HashInputs>
        && (std::is_same_v<Out, Output::InputAddressed> || std::is_same_v<Out, Output::Deferred>) );

/**
 * A derivation in the shape of the ATerm format.
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
 * so it is only ever printed and hashed. That is why `lower` and
 * `elaborate` are free functions over the shapes that do have one,
 * rather than members here.
 */
template<typename Inputs = FullInputs, typename Out = Output>
struct ATermT
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

    bool operator==(const ATermT &) const = default;

    /**
     * Parse the textual ATerm format.
     *
     * The `masked::HashInputs` cases name their input derivations by
     * hash rather than by store path. Nothing in production reads that
     * masked form, which exists to be hashed; being able to read it back is
     * what lets the tests check that the encoding is unambiguous, which
     * is what makes an input address well defined.
     *
     * @param name The derivation name (not part of the format proper;
     * it comes from the store path / file name), needed to compute
     * fixed-output paths.
     */
    static ATermT parse(
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

using ATerm = ATermT<>;
using BasicATerm = ATermT<StorePathSet>;

/**
 * Project a `Derivation` down to its ATerm shape.
 *
 * This is lossy: the parsed options are dropped, and the structured
 * attributes are re-encoded as the `__json` environment variable. See
 * `ATermT` for how faithfulness is ensured regardless.
 */
ATerm lower(const Full & drv);
BasicATerm lower(const Basic & drv);

/**
 * The inverse of `lower`: parse the legacy environment-variable
 * encodings (structured attributes, derivation options) back into a
 * `Derivation`.
 *
 * @param name The derivation name (not part of the format proper; it
 * comes from the store path / file name).
 */
Full elaborate(
    const ATerm & aterm,
    const StoreDirConfig & store,
    std::string_view name,
    const ExperimentalFeatureSettings & xpSettings = experimentalFeatureSettings);
Basic elaborate(
    const BasicATerm & aterm,
    const StoreDirConfig & store,
    std::string_view name,
    const ExperimentalFeatureSettings & xpSettings = experimentalFeatureSettings);

/**
 * Print a derivation.
 *
 * Throws if the derivation is not ATerm-representable, i.e. if its
 * first-class options are not in sync with their legacy
 * environment-variable encoding. @see ATermT.
 */
std::string unparse(
    const Full & drv, const StoreDirConfig & store, bool supportWindowsStoreDir = defaultSupportWindowsStoreDir);

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
Source & read(Source & in, const StoreDirConfig & store, BasicATerm & drv, std::string_view name);
/**
 * @param name The derivation name (not part of the format proper);
 * needed to compute fixed-output paths.
 */
void write(Sink & out, const StoreDirConfig & store, const BasicATerm & drv, std::string_view name);

} // namespace derivation

} // namespace nix
