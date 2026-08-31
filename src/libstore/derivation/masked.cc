#include "nix/store/derivation/masked.hh"
#include "nix/store/derivation/aterm.hh"
#include "nix/store/derivation/full-inputs.hh"
#include "nix/store/store-api.hh"
#include "nix/util/util.hh"

#include <boost/unordered/concurrent_flat_map.hpp>
#include <optional>

namespace nix {

namespace derivation::masked {

/* --------------------------------------------------------------------------
   Derivation hash modulo
   -------------------------------------------------------------------------- */

Hashes hashes;

/* pathInputModulo and hashInput are mutually recursive
 */

/**
 * Look up the derivation by value and memoize the `hashInput` call.
 */
static HashModulo
pathInputModulo(const StoreDirConfig & store, ReadDerivation readDerivation, const StorePath & drvPath)
{
    std::optional<HashModulo> hash;
    if (hashes.cvisit(drvPath, [&hash](const auto & kv) { hash.emplace(kv.second); })) {
        return *hash;
    }
    auto h = hashInput(store, readDerivation, readDerivation(drvPath));

    // Cache it
    hashes.insert_or_assign(drvPath, h);
    return h;
}

/**
 * Look up the hash modulo for the input derivation at `drvPath` and
 * insert the result into `drvInputs`.
 *
 * Returns `true` if deferred and cannot mutate (the caller should bail out).
 */
static bool inputModulo(
    const StoreDirConfig & store,
    ReadDerivation readDerivation,
    HashInputs::DrvMap & drvInputs,
    const StorePath & drvPath,
    const std::set<OutputName, std::less<>> & outputNames,
    std::string_view drvName)
{
    const auto & res = pathInputModulo(store, readDerivation, drvPath);
    return std::visit(
        overloaded{
            [&](const HashModulo::DeferredDrv &) { return true; },
            // Regular non-CA derivation, replace derivation
            [&](const HashModulo::DrvHash & drvHash) {
                drvInputs[drvHash].insert(outputNames.begin(), outputNames.end());
                return false;
            },
            // CA derivation's output hashes
            [&](const HashModulo::CaOutputHashes & outputHashes) {
                for (auto & outputName : outputNames) {
                    /* Put each one in with a single "out" output.. */
                    const auto h = get(outputHashes, outputName);
                    if (!h)
                        throw Error("no hash for output '%s' of derivation '%s'", outputName, drvName);
                    drvInputs[*h].insert("out");
                }
                return false;
            },
        },
        res.raw);
}

/**
 * Input masking: replace each input derivation store path with its hash
 * modulo, producing the intermediate form whose ATerm is hashed.
 *
 * The outputs are passed through untouched, whatever `Out` is; masking
 * them too, where that is wanted, is `maskOutputs`'s job and the
 * caller's choice. `hashInput` wants input masking alone, because the
 * hash it computes identifies the derivation *including* its own output
 * paths; `bothMaskedDerivation` wants both.
 *
 * Returns `std::nullopt` if any input is deferred (depends on a CA or
 * dynamic derivation whose outputs are not yet known).
 */
template<typename Out>
static std::optional<Drv<Out>>
maskInputDrvs(const StoreDirConfig & store, ReadDerivation readDerivation, Derivation<FullInputs, Out> drv)
{
    Drv<Out> substituted{
        .outputs = std::move(drv.outputs),
        .inputs{
            .srcs = std::move(drv.inputs.srcs),
            .drvs = {},
        },
        .platform = std::move(drv.platform),
        .builder = std::move(drv.builder),
        .args = std::move(drv.args),
        .env = std::move(drv.env),
        .structuredAttrs = std::move(drv.structuredAttrs),
        .name = std::move(drv.name),
    };

    for (auto & [drvPath, node] : drv.inputs.drvs.map) {
        /* Need to build and resolve dynamic derivations first */
        if (!node.childMap.empty())
            return std::nullopt;
        if (inputModulo(store, readDerivation, substituted.inputs.drvs.map, drvPath, node.value, substituted.name))
            return std::nullopt;
    }

    return substituted;
}

/* See the header for interface details. These are the implementation details.

   For fixed-output derivations, each hash in the map is not the
   corresponding output's content hash, but a hash of that hash along
   with other constant data. The key point is that the value is a pure
   function of the output's contents, and there are no preimage attacks
   either spoofing an output's contents for a derivation, or
   spoofing a derivation for an output's contents.

   For regular derivations, it looks up each subderivation from its hash
   and recurs. If the subderivation is also regular, it simply
   substitutes the derivation path with its hash. If the subderivation
   is fixed-output, however, it takes each output hash and pretends it
   is a derivation hash producing a single "out" output. This is so we
   don't leak the provenance of fixed outputs, reducing pointless cache
   misses as the build itself won't know this.
 */

/**
 * Compute the hash with outputs preserved (as `InputAddressed`).
 * Used for computing a derivation's identity as an input to other
 * derivations.
 *
 * Returns the appropriate `HashModulo` variant:
 * - `CaOutputHashes` for fixed-output CA derivations
 * - `DeferredDrv` for deferred, non-fixed CA, or impure derivations
 * - `DrvHash` for regular input-addressed derivations
 */
HashModulo hashInput(const StoreDirConfig & store, ReadDerivation readDerivation, const Full & drv)
{
    /* Return a fixed hash for fixed-output derivations. */
    if (type(drv).isFixed()) {
        std::map<std::string, Hash> outputHashes;
        for (const auto & i : drv.outputs) {
            auto & dof = std::get<Output::CAFixed>(i.second.raw);
            auto hash = hashString(
                HashAlgorithm::SHA256,
                "fixed:out:" + dof.ca.printMethodAlgo() + ":" + dof.ca.hash.to_string(HashFormat::Base16, false) + ":"
                    + store.printStorePath(dof.path(store, drv.name, i.first)));
            outputHashes.insert_or_assign(i.first, std::move(hash));
        }
        return outputHashes;
    }

    /* If any output is not InputAddressed, this derivation has no hash
       modulo. */
    for (auto & [name, output] : drv.outputs)
        if (!std::get_if<Output::InputAddressed>(&output.raw))
            return HashModulo::DeferredDrv{};

    auto inputAddressingModulo = maskInputDrvs(
        store,
        readDerivation,
        drv.mapOutputs([](const Output & output) { return std::get<Output::InputAddressed>(output.raw); })
            .mapInputs([](const std::set<SingleDerivedPath> & inputs) { return FullInputs::fromSet(inputs); }));
    if (!inputAddressingModulo)
        return HashModulo::DeferredDrv{};

    return hashString(HashAlgorithm::SHA256, unparse(*inputAddressingModulo, store));
}

/**
 * Output masking: replace the outputs with `Deferred`, and blank the
 * env vars named after them --- the output paths appear in both places,
 * so masking one without the other would leave the hash depending on
 * the derivation's own output paths anyway.
 *
 * Only valid for input-addressed (possibly deferred) derivations.
 */
template<typename Inputs, typename Out>
static Derivation<Inputs, Output::Deferred> maskOutputs(const Derivation<Inputs, Out> & drv)
    requires(
        std::is_same_v<Out, Output> || std::is_same_v<Out, Output::InputAddressed>
        || std::is_same_v<Out, Output::Deferred>)
{
    auto masked = drv.mapOutputs([](const Out & output) -> Output::Deferred {
        /* When the outputs are statically one of the input-addressing
           alternatives there is nothing to check; only the variant can
           be holding something else. */
        if constexpr (std::is_same_v<Out, Output>)
            std::visit(
                overloaded{
                    [&](const Output::InputAddressed &) {},
                    [&](const Output::Deferred &) {
                        /* Possibly pessimistically deferred --- we will fill in
                           the output paths. */
                    },
                    [&](const auto &) {
                        panic(
                            "bothMaskedDerivation: unexpected output type, these derivation types are not input addressed");
                    },
                },
                output.raw);
        return {};
    });
    for (auto & [name, output] : masked.outputs)
        if (auto j = masked.env.find(name); j != masked.env.end())
            j->second = "";
    return masked;
}

template<typename Out>
std::optional<Drv<Output::Deferred>> bothMaskedDerivation(
    const StoreDirConfig & store,
    ReadDerivation readDerivation,
    const Derivation<std::set<SingleDerivedPath>, Out> & drv)
{
    return maskInputDrvs(
        store, readDerivation, maskOutputs(drv).mapInputs([](const std::set<SingleDerivedPath> & inputs) {
            return FullInputs::fromSet(inputs);
        }));
}

template std::optional<Drv<Output::Deferred>>
bothMaskedDerivation(const StoreDirConfig & store, ReadDerivation readDerivation, const Full & drv);
template std::optional<Drv<Output::Deferred>>
bothMaskedDerivation(const StoreDirConfig & store, ReadDerivation readDerivation, const FullDeferred & drv);
template std::optional<Drv<Output::Deferred>>
bothMaskedDerivation(const StoreDirConfig & store, ReadDerivation readDerivation, const FullInputAddressed & drv);

Drv<Output::Deferred> bothMaskedDerivation(const StoreDirConfig & store, const Basic & drv)
{
    /* A resolved derivation has no input derivations, so there is
       nothing to substitute, and this cannot fail. */
    return maskOutputs(drv).mapInputs([](const StorePathSet & srcs) {
        return HashInputs{
            .srcs = srcs,
            .drvs = {},
        };
    });
}

template<typename Out>
Hash hashDerivation(const StoreDirConfig & store, const Drv<Out> & drv)
{
    return hashString(HashAlgorithm::SHA256, derivation::unparse(drv, store));
}

template Hash hashDerivation(const StoreDirConfig & store, const Drv<Output::Deferred> & drv);
template Hash hashDerivation(const StoreDirConfig & store, const Drv<Output::InputAddressed> & drv);

} // namespace derivation::masked

} // namespace nix
