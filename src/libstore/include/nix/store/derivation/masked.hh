#pragma once
///@file

#include "nix/store/derivations.hh"
#include "nix/util/fun.hh"

#include <boost/unordered/concurrent_flat_map_fwd.hpp>

namespace nix {

class Store;

namespace derivation::masked {

/**
 * Inputs in the intermediate form used to compute the hash modulo:
 * input derivations are identified by their hash modulo rather than by
 * store path.
 *
 * `Hash::operator<=>` compares bytes left-to-right, which matches
 * base16-lexicographic order (hex encoding is monotonic per byte), so
 * `std::map<Hash, ...>` gives the correct ATerm key ordering directly.
 */
struct HashInputs
{
    StorePathSet srcs;

    /**
     * No `DerivedPathMap` involved: the hash modulo is only ever
     * computed after dynamic inputs are resolved away, so each input
     * derivation maps to a plain set of output names.
     */
    using DrvMap = std::map<Hash, std::set<OutputName, std::less<>>>;

    /**
     * Nesting just to match `DerivedPathMap` for easier templating.
     */
    struct Drvs
    {
        DrvMap map;

        bool operator==(const Drvs &) const = default;
    } drvs;

    /* Needed so that `Drv`, and thus `Derivation<HashInputs, Out>`, is
       comparable: the tests compare masked derivations structurally
       rather than comparing their hashes. */
    bool operator==(const HashInputs &) const = default;
};

/**
 * An *input-masked* derivation: its input derivations are named by their
 * hash modulo rather than by store path. This is the intermediate form
 * whose ATerm encoding is hashed to compute input addresses.
 *
 * `Out` says whether the outputs are masked too:
 *
 * - `Output::InputAddressed` --- input masking only. The derivation's
 *   own output paths are still there, so its hash identifies it
 *   including them. This is what `hashInput` hashes.
 *
 * - `Output::Deferred` --- input *and* output masking, i.e. a
 *   derivation [*masked both ways*]. This is the preimage of the
 *   derivation's own input address, and what `bothMaskedDerivation`
 *   computes.
 *
 * Neither is a derivation that can be built or written to the store ---
 * their inputs name hashes, not paths --- but they are exactly what
 * input addresses are computed from, so having them as values rather
 * than only as hashes means the computation can be inspected and
 * compared.
 *
 * "Output masking" is the traditional name --- it is the "masked" store
 * derivation of `primops.cc`, blanking the output paths in the
 * `outputs` field and in the env vars named after them alike. "Input
 * masking" is the parallel name for the other half.
 *
 * @see bothMaskedDerivation, which computes the form masked both ways,
 * and hashDerivation, which hashes either.
 *
 * [*masked both ways*]:
 *   https://nix.dev/manual/nix/latest/store/derivation/outputs/input-address.html#input-masked-drv
 */
template<typename Out = Output::InputAddressed>
using Drv = Derivation<HashInputs, Out>;

/**
 * The hashes modulo of a derivation.
 *
 * Each output is given a hash, although in practice only the content-addressed
 * derivations (fixed-output or not) will have a different hash for each
 * output.
 */
struct HashModulo
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

    bool operator==(const HashModulo &) const = default;
    // auto operator <=> (const HashModulo &) const = default;

    MAKE_WRAPPER_CONSTRUCTOR(HashModulo);
};

struct HashFct
{
    using is_avalanching = std::true_type;

    std::size_t operator()(const StorePath & path) const noexcept
    {
        return std::hash<std::string_view>{}(path.to_string());
    }
};

/**
 * Memoisation of `hashInput`.
 */
typedef boost::concurrent_flat_map<StorePath, HashModulo, HashFct> Hashes;

// FIXME: global, though at least thread-safe.
extern Hashes hashes;

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
 * advance (`CAFloating` or `Impure`), `HashModulo::DeferredDrv` is
 * returned indicating we cannot yet compute an input address, because
 * we don't yet know what all the inputs are.
 */
HashModulo hashInput(const StoreDirConfig & store, ReadDerivation readDerivation, const Full & drv);

/**
 * Compute the derivation masked both ways, the preimage of a
 * derivation's own input address: input masking and output masking both, i.e. each input
 * derivation replaced by its own hash modulo (in place of its store
 * path), and the derivation's own outputs masked --- in the `outputs`
 * field and in the env vars named after them alike.
 *
 * Returns `std::nullopt` when an input's output paths are not yet
 * known, and so there is nothing to substitute for it.
 */
template<typename Out>
std::optional<Drv<Output::Deferred>> bothMaskedDerivation(
    const StoreDirConfig & store,
    ReadDerivation readDerivation,
    const Derivation<std::set<SingleDerivedPath>, Out> & drv);

extern template std::optional<Drv<Output::Deferred>>
bothMaskedDerivation(const StoreDirConfig & store, ReadDerivation readDerivation, const Full & drv);
extern template std::optional<Drv<Output::Deferred>>
bothMaskedDerivation(const StoreDirConfig & store, ReadDerivation readDerivation, const FullDeferred & drv);
extern template std::optional<Drv<Output::Deferred>>
bothMaskedDerivation(const StoreDirConfig & store, ReadDerivation readDerivation, const FullInputAddressed & drv);

/**
 * Like the above, but for a resolved (basic) derivation, which has no
 * input derivations to substitute and so cannot fail.
 */
Drv<Output::Deferred> bothMaskedDerivation(const StoreDirConfig & store, const Basic & drv);

/**
 * Hash a masked derivation. For the form masked both ways this is what
 * an input address *is*.
 *
 * This is total: the masking can fail, hashing it cannot.
 *
 * The hash algorithm is not a parameter on purpose --- "SHA-256 of this
 * ATerm" is part of the on-disk format, not a choice for callers.
 */
template<typename Out>
Hash hashDerivation(const StoreDirConfig & store, const Drv<Out> & drv);

extern template Hash hashDerivation(const StoreDirConfig & store, const Drv<Output::Deferred> & drv);
extern template Hash hashDerivation(const StoreDirConfig & store, const Drv<Output::InputAddressed> & drv);

} // namespace derivation::masked

} // namespace nix
