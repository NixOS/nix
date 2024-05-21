#pragma once
///@file

#include "nix/store/derivation/output.hh"
#include "nix/store/derivation/full-inputs.hh"

#include <boost/unordered/concurrent_flat_map_fwd.hpp>

namespace nix {

struct SingleDerivedPath;

template<typename Input, typename Output>
struct DerivationT;
using Derivation = DerivationT<SingleDerivedPath, DerivationOutput>;
using BasicDerivation = DerivationT<StorePath, DerivationOutput>;

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
/**
 * The element type corresponding to an ATerm inputs container: what the
 * inputs elaborate to.
 */
template<typename Inputs>
struct DerivationInputsElement;

template<>
struct DerivationInputsElement<FullInputs>
{
    using type = SingleDerivedPath;
};

template<>
struct DerivationInputsElement<StorePathSet>
{
    using type = StorePath;
};

template<typename Inputs = FullInputs, typename Output = DerivationOutput>
struct DerivationATermT
{
    /**
     * The derivation type this elaborates to.
     */
    using Elaborated = DerivationT<typename DerivationInputsElement<Inputs>::type, DerivationOutput>;

    DerivationOutputs<Output> outputs;
    Inputs inputs;
    std::string platform;
    std::string builder;
    Strings args;
    /**
     * Verbatim, including any legacy option-encoding variables and the
     * `__json` structured-attributes encoding.
     */
    StringPairs env;

    bool operator==(const DerivationATermT &) const = default;

    /**
     * Parse the textual ATerm format.
     */
    static DerivationATermT parse(
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
    Elaborated elaborate(const StoreDirConfig & store, std::string_view name) const;

    /**
     * Project a `Derivation` down to its ATerm shape.
     *
     * This is lossy: the parsed options are dropped, and the structured
     * attributes are re-encoded as the `__json` environment variable.
     * See the struct doc for how faithfulness is ensured regardless.
     */
    static DerivationATermT lower(const Elaborated & drv);
};

using DerivationATerm = DerivationATermT<>;
using BasicDerivationATerm = DerivationATermT<StorePathSet>;

/**
 * Read a derivation from a file.
 */
Derivation parseDerivation(
    const StoreDirConfig & store,
    std::string && s,
    std::string_view name,
    const ExperimentalFeatureSettings & xpSettings = experimentalFeatureSettings);

/**
 * The hashes modulo of a derivation.
 *
 * Each output is given a hash, although in practice only the content-addressed
 * derivations (fixed-output or not) will have a different hash for each
 * output.
 */
struct DrvHashModulo
{
    /**
     * Single hash for the derivation
     *
     * This is for an input-addressed derivation that doesn't
     * transitively depend on any floating-CA derivations.
     */
    using DrvHash = Hash;

    /**
     * Known CA drv's output hashes, for fixed-output derivations whose
     * output hashes are always known since they are fixed up-front.
     */
    using CaOutputHashes = std::map<std::string, Hash>;

    /**
     * This derivation doesn't yet have known output hashes.
     *
     * Either because itself is floating CA, or it (transtively) depends
     * on a floating CA derivation.
     */
    using DeferredDrv = std::monostate;

    using Raw = std::variant<DrvHash, CaOutputHashes, DeferredDrv>;

    Raw raw;

    bool operator==(const DrvHashModulo &) const = default;
    // auto operator <=> (const DrvHashModulo &) const = default;

    MAKE_WRAPPER_CONSTRUCTOR(DrvHashModulo);
};

/**
 * Returns hashes with the details of fixed-output subderivations
 * expunged.
 *
 * A fixed-output derivation is a derivation whose outputs have a
 * specified content hash and hash algorithm. (Currently they must have
 * exactly one output (`out`), which is specified using the `outputHash`
 * and `outputHashAlgo` attributes, but the algorithm doesn't assume
 * this.) We don't want changes to such derivations to propagate upwards
 * through the dependency graph, changing output paths everywhere.
 *
 * For instance, if we change the url in a call to the `fetchurl`
 * function, we do not want to rebuild everything depending on it---after
 * all, (the hash of) the file being downloaded is unchanged.  So the
 * *output paths* should not change. On the other hand, the *derivation
 * paths* should change to reflect the new dependency graph.
 *
 * For fixed-output derivations, this returns a map from the name of
 * each output to its hash, unique up to the output's contents.
 *
 * For regular derivations, it returns a single hash of the derivation
 * ATerm, after subderivations have been likewise expunged from that
 * derivation.
 *
 * When the derivation is itself, or (transitively) depends on, a
 * content-addressing derivation without a content address fixed in
 * advance (`CAFloating` or `Impure`), `DrvHashModulo::DeferredDrv` is
 * returned indicating we cannot yet compute an input address, because
 * we don't yet know what all the inputs are.
 */
DrvHashModulo hashInputDerivationModulo(Store & store, const Derivation & drv);

/**
 * Compute the hash with outputs masked (replaced with `Deferred`).
 * Used to compute a derivation's own output paths. Only valid for
 * input-addressed (possibly deferred) derivations.
 *
 * Returns `std::nullopt` if any input is deferred.
 */
std::optional<Hash> hashDerivationModulo(Store & store, const Derivation & drv);

struct DrvHashFct
{
    using is_avalanching = std::true_type;

    std::size_t operator()(const StorePath & path) const noexcept
    {
        return std::hash<std::string_view>{}(path.to_string());
    }
};

/**
 * Memoisation of hashInputDerivationModulo().
 */
typedef boost::concurrent_flat_map<StorePath, DrvHashModulo, DrvHashFct> DrvHashes;

// FIXME: global, though at least thread-safe.
extern DrvHashes drvHashes;

struct Source;
struct Sink;

/**
 * Wire protocol serialization of basic derivations, which also uses the
 * legacy ATerm-style representation.
 */
Source & readDerivation(Source & in, const StoreDirConfig & store, BasicDerivationATerm & drv, std::string_view name);
/**
 * @param name The derivation name (not part of the format proper);
 * needed to compute fixed-output paths.
 */
void writeDerivation(Sink & out, const StoreDirConfig & store, const BasicDerivationATerm & drv, std::string_view name);

} // namespace nix
