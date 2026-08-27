#include "nix/store/derivation/modulo.hh"
#include "nix/store/derivation/aterm.hh"
#include "nix/store/derivation/full-inputs.hh"
#include "nix/store/store-api.hh"
#include "nix/util/util.hh"

#include <boost/unordered/concurrent_flat_map.hpp>
#include <optional>

namespace nix {

namespace derivation::modulo {

/* --------------------------------------------------------------------------
   Derivation hash modulo
   -------------------------------------------------------------------------- */

Hashes hashes;

/* pathInputModulo and hashInput are mutually recursive
 */

/**
 * Look up the derivation by value and memoize the `hashInput` call.
 */
static HashModulo pathInputModulo(Store & store, const StorePath & drvPath)
{
    std::optional<HashModulo> hash;
    if (hashes.cvisit(drvPath, [&hash](const auto & kv) { hash.emplace(kv.second); })) {
        return *hash;
    }
    auto h = hashInput(store, store.readInvalidDerivation(drvPath));

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
    Store & store,
    HashInputs::DrvMap & drvInputs,
    const StorePath & drvPath,
    const std::set<OutputName, std::less<>> & outputNames,
    std::string_view drvName)
{
    const auto & res = pathInputModulo(store, drvPath);
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
 * Replace each input derivation store path with its hash modulo,
 * producing the intermediate form used to compute the derivation hash.
 *
 * When `Out` is `DerivationOutput::Deferred`, outputs are masked:
 * output paths and matching env vars are blanked so the hash does not
 * depend on its own output paths.
 *
 * When `Out` is `DerivationOutput`, outputs are preserved as-is.
 *
 * Returns `std::nullopt` if any input is deferred (depends on a CA or
 * dynamic derivation whose outputs are not yet known).
 */
template<typename Out>
static std::optional<Derivation<HashInputs, Out>> derivationModulo(Store & store, Derivation<FullInputs, Out> drv)
{
    Derivation<HashInputs, Out> masked{
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
        if (inputModulo(store, masked.inputs.drvs.map, drvPath, node.value, masked.name))
            return std::nullopt;
    }

    return masked;
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
HashModulo hashInput(Store & store, const Full & drv)
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

    auto inputAddressingModulo = derivationModulo(
        store,
        drv.mapOutputs([](const Output & output) { return std::get<Output::InputAddressed>(output.raw); })
            .mapInputs([](const std::set<SingleDerivedPath> & inputs) { return FullInputs::fromSet(inputs); }));
    if (!inputAddressingModulo)
        return HashModulo::DeferredDrv{};

    return hashString(HashAlgorithm::SHA256, unparse(*inputAddressingModulo, store));
}

/**
 * Replace the outputs with `Deferred` and blank the env vars named
 * after them, so the hash does not depend on the derivation's own
 * output paths. Only valid for input-addressed (possibly deferred)
 * derivations.
 */
template<typename Inputs>
static Derivation<Inputs, Output::Deferred> maskOutputsAndEnv(const Derivation<Inputs, Output> & drv)
{
    auto masked = drv.mapOutputs([](const Output & output) -> Output::Deferred {
        std::visit(
            overloaded{
                [&](const Output::InputAddressed &) {},
                [&](const Output::Deferred &) {
                    /* Possibly pessimistically deferred --- we will fill in
                       the output paths. */
                },
                [&](const auto &) {
                    panic("hash: unexpected output type, these derivation types are not input addressed");
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

/**
 * Compute the hash with outputs masked (replaced with `Deferred`).
 * Used by `processDerivationOutputPaths` to compute a derivation's own
 * output paths. Only valid for input-addressed (possibly deferred)
 * derivations.
 */
std::optional<std::string> unparseModulo(Store & store, const Full & drv)
{
    auto masked =
        derivationModulo(store, maskOutputsAndEnv(drv).mapInputs([](const std::set<SingleDerivedPath> & inputs) {
            return FullInputs::fromSet(inputs);
        }));
    if (!masked)
        return std::nullopt;
    return unparse(*masked, store);
}

std::string unparseModulo(Store & store, const Basic & drv)
{
    /* A resolved derivation has no input derivations, so there is
       nothing to substitute. */
    auto masked = maskOutputsAndEnv(drv).mapInputs([](const StorePathSet & srcs) {
        return HashInputs{
            .srcs = srcs,
            .drvs = {},
        };
    });

    return unparse(masked, store);
}

std::optional<Hash> hash(Store & store, const Full & drv)
{
    auto masked = unparseModulo(store, drv);
    if (!masked)
        return std::nullopt;
    return hashString(HashAlgorithm::SHA256, *masked);
}

Hash hash(Store & store, const Basic & drv)
{
    /* A resolved derivation has no input derivations, so the hash is
       always computable. */
    return hashString(HashAlgorithm::SHA256, unparseModulo(store, drv));
}

} // namespace derivation::modulo

} // namespace nix
