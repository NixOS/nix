#include "nix/store/derivations.hh"
#include "nix/store/derivation-options.hh"
#include "nix/store/derivation/aterm.hh"
#include "nix/store/downstream-placeholder.hh"
#include "nix/store/store-api.hh"
#include "nix/store/worker-protocol.hh"
#include "nix/util/types.hh"
#include "nix/util/util.hh"
#include "nix/store/common-protocol.hh"
#include "nix/store/common-protocol-impl.hh"
#include "nix/util/strings-inline.hh"
#include "nix/util/json-utils.hh"

#include <boost/container/small_vector.hpp>
#include <boost/unordered/concurrent_flat_map.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <optional>
#include <ranges>

namespace nix {

namespace derivation {

bool Type::isCA() const
{
    /* Normally we do the full `std::visit` to make sure we have
       exhaustively handled all variants, but so long as there is a
       variant called `ContentAddressed`, it must be the only one for
       which `isCA` is true for this to make sense!. */
    return std::visit(
        overloaded{
            [](const InputAddressed & ia) { return false; },
            [](const ContentAddressed & ca) { return true; },
            [](const Impure &) { return true; },
        },
        raw);
}

bool Type::isFixed() const
{
    return std::visit(
        overloaded{
            [](const InputAddressed & ia) { return false; },
            [](const ContentAddressed & ca) { return ca.fixed; },
            [](const Impure &) { return false; },
        },
        raw);
}

bool Type::hasKnownOutputPaths() const
{
    return std::visit(
        overloaded{
            [](const InputAddressed & ia) { return !ia.deferred; },
            [](const ContentAddressed & ca) { return ca.fixed; },
            [](const Impure &) { return false; },
        },
        raw);
}

bool Type::isSandboxed() const
{
    return std::visit(
        overloaded{
            [](const InputAddressed & ia) { return true; },
            [](const ContentAddressed & ca) { return ca.sandboxed; },
            [](const Impure &) { return false; },
        },
        raw);
}

bool Type::isImpure() const
{
    return std::visit(
        overloaded{
            [](const InputAddressed & ia) { return false; },
            [](const ContentAddressed & ca) { return false; },
            [](const Impure &) { return true; },
        },
        raw);
}

template<typename Inputs, typename Out>
bool Derivation<Inputs, Out>::isBuiltin() const
{
    return builder.substr(0, 8) == "builtin:";
}

} // namespace derivation

static auto infoForDerivation(const StoreDirConfig & store, const Derivation & drv)
{
    StorePathSet references;
    for (const auto & input : drv.inputs)
        references.insert(input.getBaseStorePath());
    /* Note that the outputs of a derivation are *not* references
       (that can be missing (of course) and should not necessarily be
       held during a garbage collection). */
    auto suffix = std::string(drv.name) + drvExtension;
    auto contents = unparse(drv, store);
    auto hash = hashString(HashAlgorithm::SHA256, contents);
    auto ca = TextInfo{.hash = hash, .references = references};
    return std::tuple{
        suffix,
        contents,
        references,
        store.makeFixedOutputPathFromCA(suffix, ca),
    };
}

StorePath computeStorePath(const StoreDirConfig & store, const Derivation & drv)
{
    auto [_suffix, _contents, _references, path] = infoForDerivation(store, drv);
    return path;
}

StorePath Store::writeDerivation(const Derivation & drv, RepairFlag repair)
{
    /* Check that the store supports derivation-meta before writing,
       to produce a clear error instead of a confusing hash mismatch. */
    if (drv.meta && !hasProtoFeature(WorkerProto::featureDerivationMeta)) {
        throw Error(
            "derivation '%s' uses 'derivation-meta', but the store '%s' does not support this feature; "
            "consider updating it to Nix 2.35, or disable the use of 'derivation-meta' in your derivations",
            drv.name,
            config.getHumanReadableURI());
    }

    auto [suffix, contents, references, path] = infoForDerivation(*this, drv);

    /* In case the derivation is already valid, we bail out early since that's
       faster. But we need to make sure that the derivation has a corresponding
       temproot. It is added by the remote in addToStoreFromDump, but we'd like
       to avoid sending a lot of drv contents to the daemon. */
    addTempRoot(path);

    if (isValidPath(path) && !repair)
        return path;

    StringSource s{contents};
    auto path2 = addToStoreFromDump(
        s,
        suffix,
        FileSerialisationMethod::Flat,
        ContentAddressMethod::Raw::Text,
        HashAlgorithm::SHA256,
        references,
        repair);
    assert(path2 == path);

    return path;
}

// FIXME: remove
bool isDerivation(std::string_view fileName)
{
    return hasSuffix(fileName, drvExtension);
}

std::string outputPathName(std::string_view drvName, OutputNameView outputName)
{
    using namespace std::literals::string_view_literals;

    std::string res{drvName};
    if (outputName != "out"sv) {
        res += '-';
        res += outputName;
    }
    return res;
}

namespace derivation {

template<typename Inputs>
Type type(const Derivation<Inputs, Output> & drv)
{
    using namespace std::literals::string_view_literals;

    std::optional<HashAlgorithm> floatingHashAlgo;
    std::optional<Type> ty;

    auto decide = [&](Type newTy) {
        if (!ty)
            ty = newTy;
        else if (ty.value() != newTy)
            throw Error("can't mix derivation output types");
        else if (ty.value() == Type::ContentAddressed{.sandboxed = false, .fixed = true})
            // FIXME: Experimental feature?
            throw Error("only one fixed output is allowed for now");
    };

    for (auto & i : drv.outputs) {
        std::visit(
            overloaded{
                [&](const Output::InputAddressed &) {
                    decide(
                        Type::InputAddressed{
                            .deferred = false,
                        });
                },
                [&](const Output::CAFixed &) {
                    decide(
                        Type::ContentAddressed{
                            .sandboxed = false,
                            .fixed = true,
                        });
                    if (i.first != "out"sv)
                        throw Error("single fixed output must be named \"out\"");
                },
                [&](const Output::CAFloating & dof) {
                    decide(
                        Type::ContentAddressed{
                            .sandboxed = true,
                            .fixed = false,
                        });
                    if (!floatingHashAlgo)
                        floatingHashAlgo = dof.hashAlgo;
                    else if (*floatingHashAlgo != dof.hashAlgo)
                        throw Error("all floating outputs must use the same hash algorithm");
                },
                [&](const Output::Deferred &) {
                    decide(
                        Type::InputAddressed{
                            .deferred = true,
                        });
                },
                [&](const Output::Impure &) { decide(Type::Impure{}); },
            },
            i.second.raw);
    }

    if (!ty)
        throw Error("must have at least one output");

    return ty.value();
}

template Type type(const Basic & drv);
template Type type(const Full & drv);

} // namespace derivation

namespace derivation {

template<typename Inputs, typename Out>
StringSet Derivation<Inputs, Out>::outputNames() const
{
    StringSet names;
    for (auto & i : outputs)
        names.insert(i.first);
    return names;
}

template<typename Inputs>
OutputsAndOptPaths outputsAndOptPaths(const Derivation<Inputs, Output> & drv, const StoreDirConfig & store)
{
    OutputsAndOptPaths outsAndOptPaths;
    for (auto & [outputName, output] : drv.outputs)
        outsAndOptPaths.insert(
            std::make_pair(outputName, std::make_pair(output, output.path(store, drv.name, outputName))));
    return outsAndOptPaths;
}

template OutputsAndOptPaths outputsAndOptPaths(const Basic & drv, const StoreDirConfig & store);
template OutputsAndOptPaths outputsAndOptPaths(const Full & drv, const StoreDirConfig & store);

template<typename Inputs, typename Out>
std::string_view Derivation<Inputs, Out>::nameFromPath(const StorePath & drvPath)
{
    drvPath.requireDerivation();
    auto nameWithSuffix = drvPath.name();
    nameWithSuffix.remove_suffix(drvExtension.size());
    return nameWithSuffix;
}

} // namespace derivation

std::string hashPlaceholder(const OutputNameView outputName)
{
    // FIXME: memoize?
    return "/"
           + hashString(HashAlgorithm::SHA256, concatStrings("nix-output:", outputName))
                 .to_string(HashFormat::Nix32, false);
}

namespace derivation {

template<typename Inputs, typename Out>
void Derivation<Inputs, Out>::applyRewrites(const StringMap & rewrites)
{
    if (rewrites.empty())
        return;

    debug("rewriting the derivation");

    for (auto & rewrite : rewrites)
        debug("rewriting %s as %s", rewrite.first, rewrite.second);

    builder = rewriteStrings(builder, rewrites);
    for (auto & arg : args)
        arg = rewriteStrings(arg, rewrites);

    StringPairs newEnv;
    for (auto & envVar : env) {
        auto envName = rewriteStrings(envVar.first, rewrites);
        auto envValue = rewriteStrings(envVar.second, rewrites);
        newEnv.emplace(envName, envValue);
    }
    env = std::move(newEnv);

    if (structuredAttrs) {
        // TODO rewrite the JSON AST properly, rather than dump parse round trip.
        auto [_, jsonS] = structuredAttrs->unparse();
        jsonS = rewriteStrings(std::move(jsonS), rewrites);
        structuredAttrs = StructuredAttrs::parse(jsonS);
    }
}

Full unresolve(const Basic & drv)
{
    return drv.mapInputs([](const StorePathSet & inputs) -> std::set<SingleDerivedPath> {
        auto view = inputs | std::views::transform([](const StorePath & p) -> SingleDerivedPath {
                        return SingleDerivedPath::Opaque{p};
                    });
        return std::set<SingleDerivedPath>(view.begin(), view.end());
    });
}

bool shouldResolve(const Full & drv)
{
    bool hasInputDrvs = std::ranges::any_of(
        drv.inputs, [](const auto & input) { return std::holds_alternative<SingleDerivedPath::Built>(input.raw()); });

    /* No input drvs means nothing to resolve. */
    if (!hasInputDrvs)
        return false;

    auto drvType = type(drv);

    bool typeNeedsResolve = std::visit(
        overloaded{
            [&](const Type::InputAddressed & ia) {
                /* Must resolve if deferred. */
                return ia.deferred;
            },
            [&](const Type::ContentAddressed & ca) {
                return ca.fixed
                           /* Can optionally resolve if fixed, which is good
                              for avoiding unnecessary rebuilds. */
                           ? experimentalFeatureSettings.isEnabled(Xp::CaDerivations)
                           /* Must resolve if floating. */
                           : true;
            },
            [&](const Type::Impure &) { return true; },
        },
        drvType.raw);

    return typeNeedsResolve ||
           /* Also need to resolve if any inputs are outputs of dynamic derivations. */
           hasDynamicDrvDep(drv.inputs);
}

bool hasDynamicDrvDep(const std::set<SingleDerivedPath> & inputs)
{
    return std::ranges::any_of(inputs, [](const auto & input) { return input.isDynamicDrvOutput(); });
}

template<bool fillIn>
static void processDerivationOutputPaths(Store & store, auto && drv, std::string_view drvName);

std::optional<Basic> tryResolve(const Full & drv, Store & store, Store * evalStore)
{
    return tryResolve(
        drv,
        store,
        [&](ref<const SingleDerivedPath> drvPath, const std::string & outputName) -> std::optional<StorePath> {
            try {
                return resolveDerivedPath(store, SingleDerivedPath::Built{drvPath, outputName}, evalStore);
            } catch (Error &) {
                return std::nullopt;
            }
        });
}

std::optional<Basic> tryResolve(
    const Full & drv,
    Store & store,
    fun<std::optional<StorePath>(ref<const SingleDerivedPath> drvPath, const std::string & outputName)>
        queryResolutionChain)
{
    StorePathSet resolvedInputs;
    StringMap inputRewrites;

    for (const auto & input : drv.inputs) {
        auto resolved = std::visit(
            overloaded{
                [&](const SingleDerivedPath::Opaque & op) -> std::optional<StorePath> { return op.path; },
                [&](const SingleDerivedPath::Built & built) -> std::optional<StorePath> {
                    auto actualPathOpt = queryResolutionChain(built.drvPath, built.output);
                    if (!actualPathOpt)
                        return std::nullopt;

                    if (experimentalFeatureSettings.isEnabled(Xp::CaDerivations)) {
                        /* This handles both the static case (opaque
                           derivation path) and the dynamic case
                           (derivation path that is itself an output of
                           a derivation), recursively. */
                        auto placeholder = DownstreamPlaceholder::fromSingleDerivedPathBuilt(built);
                        inputRewrites.emplace(placeholder.render(), store.printStorePath(*actualPathOpt));
                    }

                    return actualPathOpt;
                },
            },
            input.raw());

        if (!resolved)
            return std::nullopt;
        resolvedInputs.insert(*resolved);
    }

    Basic result{
        .outputs = drv.outputs,
        .inputs = resolvedInputs,
        .platform = drv.platform,
        .builder = drv.builder,
        .args = drv.args,
        .env = drv.env,
        .structuredAttrs = drv.structuredAttrs,
        .name = drv.name,
    };

    result.applyRewrites(inputRewrites);

    processDerivationOutputPaths</*fillIn=*/true>(store, result, result.name);

    return result;
}

/**
 * Process `InputAddressed`, `Deferred`, and `CAFixed` outputs.
 *
 * For `InputAddressed` outputs or `Deferred` outputs:
 *
 * - with `Regular` hash kind, validate `InputAddressed` outputs have
 *   the correct path (throws if mismatch). For `Deferred` outputs:
 *   - if `fillIn` is true, fill in the output path to make `InputAddressed`
 *   - if `fillIn` is false, throw an error
 *   Then validate or fill in the environment variable with the path.
 *
 * - with `Deferred` hash kind, validate that the output is either
 *   `InputAddressed` (error) or `Deferred` (correct).
 *
 * For `CAFixed` outputs, validate or fill in the environment variable
 * with the computed path.
 *
 * @tparam fillIn If true, fill in missing output paths and environment
 * variables. If false, validate that all paths are correct (throws on
 * mismatch).
 */
template<bool fillIn>
static void processDerivationOutputPaths(Store & store, auto && drv, std::string_view drvName)
{
    /* output optional is for whether we set it yet. Inner optional is
       for whether the input-addressed derivation has an input address
       now or is deferred --- can only calculate input address later. */
    std::optional<std::optional<Hash>> hashModulo_;

    auto hashModulo = [&]() -> const std::optional<Hash> & {
        if (!hashModulo_) {
            // somewhat expensive so we do lazily
            hashModulo_ = derivation::hashModulo(store, drv);
        }
        return *hashModulo_;
    };

    /* With the `builder-rpc-v0` experimental feature, outputs are
       communicated to the builder over RPC rather than via environment
       variables, so there are no environment variables named after
       outputs to fill in or validate. Parsing the full
       `derivation::Options` here also has the nice side effect that
       validation catches malformed options. When the derivation has
       input derivations, they must be passed along so that output
       placeholders in path-valued options (e.g.
       `exportReferencesGraph`) are recognized. */
    bool rpcOutputs = [&] {
        auto * parsed = get(drv.structuredAttrs);
        if constexpr (std::is_same_v<std::decay_t<decltype(drv.inputs)>, std::set<SingleDerivedPath>>)
            return derivationOptionsFromStructuredAttrs(store, drv.inputs, drv.env, parsed, /*shouldWarn=*/false)
                       .requiredSystemFeatures.count(std::string{drvFeatureBuilderRpcV0})
                   != 0;
        else
            return derivationOptionsFromStructuredAttrs(store, drv.env, parsed, /*shouldWarn=*/false)
                       .requiredSystemFeatures.count(std::string{drvFeatureBuilderRpcV0})
                   != 0;
    }();

    /* Throws on invalid output combinations. Must run before
       `hashModulo`, which would panic instead. */
    auto drvType = type(drv);

    if (rpcOutputs && std::holds_alternative<Type::InputAddressed>(drvType.raw))
        throw Error(
            "derivation uses the '%s' feature, which may only be used with content-addressing derivations",
            drvFeatureBuilderRpcV0);

    for (auto & [outputName, output] : drv.outputs) {
        auto envHasRightPath = [&](const StorePath & actual, bool isDeferred = false) {
            if (rpcOutputs)
                return;
            if constexpr (fillIn) {
                auto j = drv.env.find(outputName);
                /* Fill in mode: fill in missing or empty environment
                   variables */
                if (j == drv.env.end())
                    drv.env.insert(j, {outputName, store.printStorePath(actual)});
                else if (j->second == "")
                    j->second = store.printStorePath(actual);
                /* We know validation will succeed after fill-in, but
                   just to be extra sure, validate unconditionally */
            }
            auto j = drv.env.find(outputName);
            if (j == drv.env.end())
                throw Error(
                    "derivation has missing environment variable '%s', should be '%s' but is not present",
                    outputName,
                    store.printStorePath(actual));
            if (j->second != store.printStorePath(actual)) {
                if (isDeferred) {
                    warn(
                        "derivation has incorrect environment variable '%s', should be '%s' but is actually '%s'\nThis will be an error in future versions of Nix; compatibility of CA derivations will be broken.",
                        outputName,
                        store.printStorePath(actual),
                        j->second);
                    /* Fix the env var so a later `checkInvariants`
                       doesn't reject it. */
                    if constexpr (fillIn)
                        j->second = store.printStorePath(actual);
                } else
                    throw Error(
                        "derivation has incorrect environment variable '%s', should be '%s' but is actually '%s'",
                        outputName,
                        store.printStorePath(actual),
                        j->second);
            }
        };
        auto hash = [&]<typename Out>(const Out & outputVariant) {
            auto & drvHash = hashModulo();
            if (drvHash) {
                auto outPath = store.makeOutputPath(outputName, *drvHash, drvName);

                if constexpr (std::is_same_v<Out, Output::InputAddressed>) {
                    if (outputVariant.path == outPath) {
                        envHasRightPath(outPath);
                        return; // Correct case
                    }
                    /* Error case, an explicitly wrong path is
                       always an error. */
                    throw Error(
                        "derivation has incorrect output '%s', should be '%s'",
                        store.printStorePath(outputVariant.path),
                        store.printStorePath(outPath));
                } else if constexpr (std::is_same_v<Out, Output::Deferred>) {
                    if constexpr (fillIn) {
                        /* Fill in output path for Deferred outputs */
                        output = Output::InputAddressed{
                            .path = outPath,
                        };
                        /* A pre-existing incorrect env var for a
                           formerly-deferred output only warns, for
                           compatibility with derivations produced by
                           older versions of Nix. */
                        envHasRightPath(outPath, /*isDeferred=*/true);
                    } else {
                        /* Validation mode: deferred outputs
                           should have been filled in */
                        warn(
                            "derivation has incorrect deferred output, should be '%s'.\nThis will be an error in future versions of Nix; compatibility of CA derivations will be broken.",
                            store.printStorePath(outPath));
                    }
                } else {
                    /* Will never happen, based on where
                       `hash` is called. */
                    static_assert(false);
                }
            } else {
                /* Deferred --- hash not yet known. */
                if constexpr (std::is_same_v<Out, Output::InputAddressed>) {
                    /* Error case, an explicitly wrong path is
                       always an error. */
                    throw Error(
                        "derivation has incorrect output '%s', should be deferred",
                        store.printStorePath(outputVariant.path));
                } else if constexpr (std::is_same_v<Out, Output::Deferred>) {
                    /* Correct: Deferred output with Deferred hash kind. */
                } else {
                    /* Will never happen, based on where
                       `hash` is called. */
                    static_assert(false);
                }
            }
        };
        std::visit(
            overloaded{
                [&](const Output::InputAddressed & o) { hash(o); },
                [&](const Output::Deferred & o) { hash(o); },
                [&](const Output::CAFixed & dof) { envHasRightPath(dof.path(store, drvName, outputName)); },
                [&](const auto &) {
                    // Nothing to do for other output types
                },
            },
            output.raw);
    }
}

template<typename Inputs>
void checkInvariants(const Derivation<Inputs, Output> & drv, Store & store, const StorePath & drvPath)
{
    assert(drvPath.isDerivation());
    std::string drvName(drvPath.name());
    drvName = drvName.substr(0, drvName.size() - drvExtension.size());

    if (drvName != drv.name) {
        throw Error(
            "derivation '%s' has name '%s' which does not match its path", store.printStorePath(drvPath), drv.name);
    }

    try {
        checkInvariants(drv, store);
    } catch (Error & e) {
        e.addTrace({}, "while checking derivation '%s'", store.printStorePath(drvPath));
        throw;
    }
}

template void checkInvariants(const Basic & drv, Store & store, const StorePath & drvPath);
template void checkInvariants(const Full & drv, Store & store, const StorePath & drvPath);

void checkCanonicalRequiredSystemFeatures(
    std::string_view drvName, const nlohmann::json & requiredSystemFeatures, std::string_view context)
{
    auto features = getStringList(requiredSystemFeatures);

    // Check sorted and unique. This allows e.g. derivation-meta to reliably
    // convert from high-level representation to ATerm, without having to track
    // where into the system features to insert. That wouldn't be high-level.
    if (!std::ranges::is_sorted(features))
        throw Error(
            "derivation '%s' has unsorted 'requiredSystemFeatures', which is not permitted %s", drvName, context);
    if (auto dup = std::ranges::adjacent_find(features); dup != features.end())
        throw Error(
            "derivation '%s' has a duplicate 'requiredSystemFeatures' entry '%s', which is not permitted %s",
            drvName,
            *dup,
            context);
}

/* Considering the significance of the ATerm format, we make sure that the
   high-level representation is clean and representable unambiguously in ATerm.

   Specifically it means that `unparse` and `write` need to work, or equivalently,
   the derivation must be a possible `derivation::extraMeta` return value.

   Allowing these invariants to be violated means it would be possible to
   construct a JSON that doesn't roundtrip to ATerm. */
static void checkMetaInvariant(const auto & drv)
{
    if (!drv.meta)
        return;

    /* `meta` is only representable as part of structured attributes (see
       `Derivation::meta`). Without them there is nowhere to serialise it, so it
       would be silently dropped. Reject this rather than lose data. */
    if (!drv.structuredAttrs)
        throw Error(
            "derivation '%s' has 'meta' but no structured attributes; 'meta' requires structured attributes", drv.name);

    auto & sa = drv.structuredAttrs->structuredAttrs;

    if (sa.contains("__meta"))
        throw Error(
            "derivation '%s' must not set the '__meta' structured attribute, because that conflicts with the encoding of 'meta' in the current derivation format", // which is ATerm
            drv.name);

    auto rsfIt = sa.find("requiredSystemFeatures");
    if (rsfIt == sa.end() || !rsfIt->second.is_array())
        return;

    // `derivation-meta` is implied by `meta` and reinjected on serialisation, so
    // in the clean representation it must be absent. This is the high-level
    // counterpart of `extractMeta` requiring it to be *present* in the encoding.
    for (const auto & feature : rsfIt->second)
        if (feature.is_string() && feature.template get<std::string>() == "derivation-meta")
            throw Error(
                "derivation '%s' must not list 'derivation-meta' in 'requiredSystemFeatures'; it is implied by 'meta' != null",
                drv.name);

    // Shared with `extractMeta`: the ordering/duplicate requirement is the same
    // on the clean list here as on the encoded list there. */
    checkCanonicalRequiredSystemFeatures(drv.name, rsfIt->second, "when 'meta' is set");
}

void checkInvariants(const Basic & drv, Store & store)
{
    checkMetaInvariant(drv);
    processDerivationOutputPaths<false>(store, drv, drv.name);
}

void checkInvariants(const Full & drv, Store & store)
{
    checkMetaInvariant(drv);
    processDerivationOutputPaths<false>(store, drv, drv.name);
}

void fillInOutputPaths(Full & drv, Store & store)
{
    processDerivationOutputPaths<true>(store, drv, drv.name);
}

Full parseJsonAndValidate(Store & store, const nlohmann::json & json)
{
    auto drv = static_cast<Full>(json);

    fillInOutputPaths(drv, store);

    try {
        checkInvariants(drv, store);
    } catch (Error & e) {
        e.addTrace({}, "while checking derivation from JSON with name '%s'", drv.name);
        throw;
    }

    return drv;
}

} // namespace derivation

const Hash impureOutputHash = hashString(HashAlgorithm::SHA256, "impure");

// Explicit template instantiations
namespace derivation {

template struct Derivation<StorePathSet>;
template struct Derivation<std::set<SingleDerivedPath>>;

} // namespace derivation

} // namespace nix
