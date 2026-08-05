#include "nix/store/derivation/masked.hh"
#include "nix/store/derivation/aterm.hh"
#include "nix/store/derivation/full-inputs.hh"
#include "nix/store/store-api.hh"
#include "nix/store/derivation/elaborate.hh"
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
 * modulo, producing the inputs of the intermediate form whose ATerm is
 * hashed.
 *
 * Only the inputs are dealt with here; masking the outputs too, where
 * that is wanted, is `maskOutputs`'s job and the caller's choice.
 * `hashInput` wants input masking alone, because the hash it computes
 * identifies the derivation *including* its own output paths;
 * `bothMaskedDerivation` wants both.
 *
 * Returns `std::nullopt` if any input is deferred (depends on a CA or
 * dynamic derivation whose outputs are not yet known).
 */
static std::optional<HashInputs> maskInputDrvs(
    const StoreDirConfig & store, ReadDerivation readDerivation, FullInputs inputs, std::string_view drvName)
{
    HashInputs substituted{
        .srcs = std::move(inputs.srcs),
        .drvs = {},
    };

    for (auto & [drvPath, node] : inputs.drvs.map) {
        /* Need to build and resolve dynamic derivations first */
        if (!node.childMap.empty())
            return std::nullopt;
        if (inputModulo(store, readDerivation, substituted.drvs.map, drvPath, node.value, drvName))
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
            auto & dof = std::get<Output::CAFixed>(i.second.output.raw);
            auto hash = hashString(
                HashAlgorithm::SHA256,
                "fixed:out:" + dof.ca.printMethodAlgo() + ":" + dof.ca.hash.to_string(HashFormat::Base16, false) + ":"
                    + store.printStorePath(dof.path(store, drv.name, i.first)));
            outputHashes.insert_or_assign(i.first, std::move(hash));
        }
        return outputHashes;
    }

    /* Extract InputAddressed outputs. If any output is not
       InputAddressed, this derivation has no hash modulo. */
    Outputs<Output::InputAddressed> convertedOutputs;
    for (auto & [name, output] : drv.outputs) {
        auto * p = std::get_if<Output::InputAddressed>(&output.output.raw);
        if (!p)
            return HashModulo::DeferredDrv{};
        convertedOutputs.insert({name, *p});
    }

    StringPairs env;
    for (auto & [name, var] : drv.env)
        env.insert_or_assign(name, var.value);
    StructuredAttrs::checkKeyNotInUse(env);
    if (drv.structuredAttrs)
        env.insert(drv.structuredAttrs->unparse());

    auto substitutedInputs =
        maskInputDrvs(store, readDerivation, FullInputs::fromSet(drv.inputs), drv.name);
    if (!substitutedInputs)
        return HashModulo::DeferredDrv{};

    Drv<Output::InputAddressed> inputAddressingModulo{
        .outputs = std::move(convertedOutputs),
        .inputs = std::move(*substitutedInputs),
        .platform = drv.platform,
        .builder = drv.builder,
        .args = drv.args,
        .env = std::move(env),
    };
    return hashDerivation(store, inputAddressingModulo);
}

/**
 * Output masking: build the ATerm-shaped intermediate whose outputs are
 * replaced with `Deferred`, and whose env vars named after them are
 * blanked --- the output paths appear in both places, so masking one
 * without the other would leave the hash depending on the derivation's
 * own output paths anyway.
 *
 * Only valid for input-addressed (possibly deferred) derivations.
 *
 * The inputs are already substituted by the caller, which is the only
 * step that can fail.
 */
template<typename Input, typename Out>
static Drv<Output::Deferred> maskOutputs(const Derivation<Input, Out> & drv, HashInputs inputs)
    requires(
        std::is_same_v<Out, Output> || std::is_same_v<Out, Output::InputAddressed>
        || std::is_same_v<Out, Output::Deferred>)
{
    StringPairs env;
    for (auto & [name, var] : drv.env)
        env.insert_or_assign(name, var.value);

    Outputs<Output::Deferred> maskedOutputs;
    for (auto & [name, output] : drv.outputs) {
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
                output.output.raw);
        maskedOutputs.insert({name, {}});
        if (auto j = env.find(name); j != env.end())
            j->second = "";
    }

    StructuredAttrs::checkKeyNotInUse(env);
    if (drv.structuredAttrs)
        env.insert(drv.structuredAttrs->unparse());

    return {
        .outputs = std::move(maskedOutputs),
        .inputs = std::move(inputs),
        .platform = drv.platform,
        .builder = drv.builder,
        .args = drv.args,
        .env = std::move(env),
    };
}

template<typename Out>
std::optional<Drv<Output::Deferred>> bothMaskedDerivation(
    const StoreDirConfig & store,
    ReadDerivation readDerivation,
    const Derivation<SingleDerivedPath, Out> & drv)
{
    auto substitutedInputs = maskInputDrvs(store, readDerivation, FullInputs::fromSet(drv.inputs), drv.name);
    if (!substitutedInputs)
        return std::nullopt;
    return maskOutputs(drv, std::move(*substitutedInputs));
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
    return maskOutputs(
        drv,
        HashInputs{
            .srcs = drv.inputs,
            .drvs = {},
        });
}

template<typename Out>
Hash hashDerivation(const StoreDirConfig & store, const Drv<Out> & drv)
{
    /* The derivation name is only needed to compute the paths of
       fixed-output outputs, which the modulo form never has: its
       outputs are input-addressed or masked. */
    return hashString(HashAlgorithm::SHA256, drv.to_string(store, /*name=*/""));
}

template Hash hashDerivation(const StoreDirConfig & store, const Drv<Output::Deferred> & drv);
template Hash hashDerivation(const StoreDirConfig & store, const Drv<Output::InputAddressed> & drv);

} // namespace derivation::masked

} // namespace nix
