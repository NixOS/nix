#pragma once
///@file

#include "nix/store/derivations.hh"

#include <boost/unordered/concurrent_flat_map_fwd.hpp>

namespace nix {

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

Source & readDerivation(Source & in, const StoreDirConfig & store, BasicDerivation & drv, std::string_view name);
void writeDerivation(Sink & out, const StoreDirConfig & store, const BasicDerivation & drv);

} // namespace nix
