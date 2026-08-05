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
 * The element type corresponding to an ATerm inputs container: what the
 * inputs elaborate to.
 */
template<typename Inputs>
struct InputsElement;

template<>
struct InputsElement<FullInputs>
{
    using type = SingleDerivedPath;
};

template<>
struct InputsElement<StorePathSet>
{
    using type = StorePath;
};

/**
 * A derivation in the shape of the ATerm format.
 *
 * This corresponds closely to the on-disk `.drv` format: inputs are
 * split into sources and derivations, and the environment is kept
 * verbatim — including the legacy encodings of the various `Derivation`
 * options, and the `__json` encoding of structured attributes.
 *
 * It is a *lossy projection* of `Derivation`: `lower` simply drops the
 * parsed options and structured attributes, keeping only the raw
 * environment. Faithfulness is checked by round-tripping: a
 * `Derivation` is representable in the ATerm format iff
 * `lower(drv).elaborate(store) == drv`, i.e. iff its parsed fields are
 * in sync with their environment-variable encoding. Derivations whose
 * options are specified directly (e.g. via newer formats like JSON)
 * without the legacy environment encoding are not ATerm-representable.
 */
template<typename Inputs = FullInputs, typename Out = Output>
struct ATermT
{
    /**
     * The derivation type this elaborates to.
     */
    using Elaborated = Derivation<typename InputsElement<Inputs>::type, Output>;

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
     */
    static ATermT parse(
        const StoreDirConfig & store,
        std::string && s,
        std::string_view name,
        const ExperimentalFeatureSettings & xpSettings = experimentalFeatureSettings);

    /**
     * Print to the textual ATerm format.
     *
     * @param name The derivation name (not part of the format proper);
     * needed to compute fixed-output paths.
     */
    std::string to_string(const StoreDirConfig & store, std::string_view name) const;

    /**
     * Parse the legacy environment-variable encodings (structured
     * attributes, derivation options) into a full `Derivation`.
     *
     * @param name The derivation name (not part of the format proper;
     * it comes from the store path / file name).
     */
    Elaborated elaborate(
        const StoreDirConfig & store,
        std::string_view name,
        const ExperimentalFeatureSettings & xpSettings = experimentalFeatureSettings) const;

    /**
     * Project a `Derivation` down to its ATerm shape.
     *
     * This is lossy: the parsed options are dropped, and the structured
     * attributes are re-encoded as the `__json` environment variable.
     * See the struct doc for how faithfulness is ensured regardless.
     */
    static ATermT lower(const Elaborated & drv);
};

using ATerm = ATermT<>;
using BasicATerm = ATermT<StorePathSet>;

/**
 * Print a derivation.
 */
std::string unparse(const Full & drv, const StoreDirConfig & store);

namespace modulo {
struct HashInputs;
}

/**
 * Print a derivation in ATerm shape, in one of the intermediate forms:
 * with the inputs already flattened (`FullInputs`), or with them
 * replaced by their hashes modulo (`modulo::HashInputs`), which is the
 * form whose hash is an input address.
 *
 * The `inputs` to serialize are passed separately, in one of those
 * format-oriented representations; `drv.inputs` itself is ignored.
 *
 * The `modulo::HashInputs` cases are not round-trippable: `parse` cannot
 * read them back, as their input derivations are named by hash rather
 * than by store path.
 */
template<
    typename Inputs,
    typename Drv,
    typename Out = typename std::decay_t<decltype(std::declval<Drv>().outputs)>::mapped_type>
std::string
unparseDerivation(const StoreDirConfig & store, const Inputs & inputs, std::string_view drvName, const Drv & drv)
    requires(
        // Regular `FullInputs` case must have regular `Output` outputs
        (std::is_same_v<Inputs, FullInputs> && std::is_same_v<Out, Output>)
        // Hash modulo is only for input addressing, with masked (`Deferred`) or unmasked (`InputAddressed`) outputs
        || (std::is_same_v<Inputs, modulo::HashInputs>
            && (std::is_same_v<Out, Output::InputAddressed> || std::is_same_v<Out, Output::Deferred>) ));

/**
 * Read a derivation from a file.
 */
Full parse(
    const StoreDirConfig & store,
    std::string && s,
    std::string_view name,
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
