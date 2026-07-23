#include "nix/store/derivation/aterm.hh"
#include "nix/store/derivations.hh"
#include "nix/store/derivation/elaborate.hh"
#include "nix/store/worker-settings.hh"
#include "nix/store/derivation/full-inputs.hh"
#include "nix/store/downstream-placeholder.hh"
#include "nix/store/store-api.hh"
#include "nix/util/types.hh"
#include "nix/util/util.hh"
#include "nix/store/common-protocol.hh"
#include "nix/store/common-protocol-impl.hh"
#include "nix/util/strings-inline.hh"
#include <boost/container/small_vector.hpp>
#include <boost/unordered/concurrent_flat_map.hpp>
#include <algorithm>
#include <optional>
#include <ranges>

namespace nix {

bool DerivationType::isCA() const
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

bool DerivationType::isFixed() const
{
    return std::visit(
        overloaded{
            [](const InputAddressed & ia) { return false; },
            [](const ContentAddressed & ca) { return ca.fixed; },
            [](const Impure &) { return false; },
        },
        raw);
}

bool DerivationType::hasKnownOutputPaths() const
{
    return std::visit(
        overloaded{
            [](const InputAddressed & ia) { return !ia.deferred; },
            [](const ContentAddressed & ca) { return ca.fixed; },
            [](const Impure &) { return false; },
        },
        raw);
}

bool DerivationType::isSandboxed() const
{
    return std::visit(
        overloaded{
            [](const InputAddressed & ia) { return true; },
            [](const ContentAddressed & ca) { return ca.sandboxed; },
            [](const Impure &) { return false; },
        },
        raw);
}

bool DerivationType::isImpure() const
{
    return std::visit(
        overloaded{
            [](const InputAddressed & ia) { return false; },
            [](const ContentAddressed & ca) { return false; },
            [](const Impure &) { return true; },
        },
        raw);
}

bool isBuiltin(const BasicDerivation & drv)
{
    return drv.builder.substr(0, 8) == "builtin:";
}

template<typename Inputs, typename Output>
bool DerivationT<Inputs, Output>::isBuiltin() const
    requires std::is_same_v<Output, DerivationOutput>
{
    return builder.substr(0, 8) == "builtin:";
}

// Forward declaration of specialization
template<>
std::string Derivation::unparse(const StoreDirConfig & store) const;

static auto infoForDerivation(const StoreDirConfig & store, const Derivation & drv)
{
    StorePathSet references;
    for (const auto & input : drv.inputs)
        references.insert(input.getBaseStorePath());
    /* Note that the outputs of a derivation are *not* references
       (that can be missing (of course) and should not necessarily be
       held during a garbage collection). */
    auto suffix = std::string(drv.name) + drvExtension;
    auto contents = drv.unparse(store);
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

template<typename Inputs, typename Output>
DerivationType DerivationT<Inputs, Output>::type() const
    requires std::is_same_v<Output, DerivationOutput>
{
    using namespace std::literals::string_view_literals;

    std::optional<HashAlgorithm> floatingHashAlgo;
    std::optional<DerivationType> ty;

    auto decide = [&](DerivationType newTy) {
        if (!ty)
            ty = newTy;
        else if (ty.value() != newTy)
            throw Error("can't mix derivation output types");
        else if (ty.value() == DerivationType::ContentAddressed{.sandboxed = false, .fixed = true})
            // FIXME: Experimental feature?
            throw Error("only one fixed output is allowed for now");
    };

    for (auto & i : outputs) {
        std::visit(
            overloaded{
                [&](const DerivationOutput::InputAddressed &) {
                    decide(
                        DerivationType::InputAddressed{
                            .deferred = false,
                        });
                },
                [&](const DerivationOutput::CAFixed &) {
                    decide(
                        DerivationType::ContentAddressed{
                            .sandboxed = false,
                            .fixed = true,
                        });
                    if (i.first != "out"sv)
                        throw Error("single fixed output must be named \"out\"");
                },
                [&](const DerivationOutput::CAFloating & dof) {
                    decide(
                        DerivationType::ContentAddressed{
                            .sandboxed = true,
                            .fixed = false,
                        });
                    if (!floatingHashAlgo)
                        floatingHashAlgo = dof.hashAlgo;
                    else if (*floatingHashAlgo != dof.hashAlgo)
                        throw Error("all floating outputs must use the same hash algorithm");
                },
                [&](const DerivationOutput::Deferred &) {
                    decide(
                        DerivationType::InputAddressed{
                            .deferred = true,
                        });
                },
                [&](const DerivationOutput::Impure &) { decide(DerivationType::Impure{}); },
            },
            i.second.output.raw);
    }

    if (!ty)
        throw Error("must have at least one output");

    return ty.value();
}

template<typename Inputs, typename Output>
StringSet DerivationT<Inputs, Output>::getRequiredSystemFeatures() const
    requires std::is_same_v<Output, DerivationOutput>
{
    // FIXME: cache this?
    StringSet res;
    for (auto & i : options.requiredSystemFeatures)
        res.insert(i);
    if (!type().hasKnownOutputPaths())
        res.insert("ca-derivations");
    return res;
}

template<typename Inputs, typename Output>
bool DerivationT<Inputs, Output>::useUidRange() const
    requires std::is_same_v<Output, DerivationOutput>
{
    return getRequiredSystemFeatures().count("uid-range");
}

template<typename Inputs, typename Output>
StringSet DerivationT<Inputs, Output>::outputNames() const
    requires std::is_same_v<Output, DerivationOutput>
{
    StringSet names;
    for (auto & i : outputs)
        names.insert(i.first);
    return names;
}

template<typename Inputs, typename Output>
DerivationOutputsAndOptPaths DerivationT<Inputs, Output>::outputsAndOptPaths(const StoreDirConfig & store) const
    requires std::is_same_v<Output, DerivationOutput>
{
    DerivationOutputsAndOptPaths outsAndOptPaths;
    for (auto & [outputName, output] : outputs)
        outsAndOptPaths.insert(
            std::make_pair(outputName, std::make_pair(output.output, output.output.path(store, name, outputName))));
    return outsAndOptPaths;
}

template<typename Inputs, typename Output>
std::string_view DerivationT<Inputs, Output>::nameFromPath(const StorePath & drvPath)
{
    drvPath.requireDerivation();
    auto nameWithSuffix = drvPath.name();
    nameWithSuffix.remove_suffix(drvExtension.size());
    return nameWithSuffix;
}

std::string hashPlaceholder(const OutputNameView outputName)
{
    // FIXME: memoize?
    return "/"
           + hashString(HashAlgorithm::SHA256, concatStrings("nix-output:", outputName))
                 .to_string(HashFormat::Nix32, false);
}

template<typename Inputs, typename Output>
void DerivationT<Inputs, Output>::applyRewrites(const StringMap & rewrites)
    requires std::is_same_v<Output, DerivationOutput>
{
    if (rewrites.empty())
        return;

    debug("rewriting the derivation");

    for (auto & rewrite : rewrites)
        debug("rewriting %s as %s", rewrite.first, rewrite.second);

    builder = rewriteStrings(builder, rewrites);
    for (auto & arg : args)
        arg = rewriteStrings(arg, rewrites);

    decltype(env) newEnv;
    for (auto & envVar : env) {
        auto envName = rewriteStrings(envVar.first, rewrites);
        auto envValue = rewriteStrings(envVar.second.value, rewrites);
        newEnv.emplace(
            std::move(envName),
            derivation::EnvValue{
                .value = std::move(envValue),
                .passAsFile = envVar.second.passAsFile,
            });
    }
    env = std::move(newEnv);

    if (structuredAttrs) {
        // TODO rewrite the JSON AST properly, rather than dump parse round trip.
        auto [_, jsonS] = structuredAttrs->unparse();
        jsonS = rewriteStrings(std::move(jsonS), rewrites);
        structuredAttrs = StructuredAttrs::parse(jsonS);
    }
}

template<>
Derivation DerivationT<StorePath>::unresolve() const
{
    auto res = mapInputs([](const StorePathSet & inputs) -> std::set<SingleDerivedPath> {
        auto view = inputs | std::views::transform([](const StorePath & p) -> SingleDerivedPath {
                        return SingleDerivedPath::Opaque{p};
                    });
        return std::set<SingleDerivedPath>(view.begin(), view.end());
    });

    /* Inject the option fields; store paths trivially become opaque
       deriving paths. This cannot fail. */
    auto injectRef = [](const DrvRef<StorePath> & ref) -> DrvRef<SingleDerivedPath> {
        return std::visit(
            overloaded{
                [](const OutputName & outputName) -> DrvRef<SingleDerivedPath> { return outputName; },
                [](const StorePath & path) -> DrvRef<SingleDerivedPath> { return SingleDerivedPath::Opaque{path}; },
            },
            ref);
    };
    auto injectRefSet = [&](const std::set<DrvRef<StorePath>> & refs) {
        std::set<DrvRef<SingleDerivedPath>> res;
        for (auto & ref : refs)
            res.insert(injectRef(ref));
        return res;
    };
    auto injectChecks = [&](const derivation::OutputChecks<StorePath> & checks) {
        return derivation::OutputChecks<SingleDerivedPath>{
            .ignoreSelfRefs = checks.ignoreSelfRefs,
            .maxSize = checks.maxSize,
            .maxClosureSize = checks.maxClosureSize,
            .allowedReferences =
                checks.allowedReferences ? std::optional{injectRefSet(*checks.allowedReferences)} : std::nullopt,
            .disallowedReferences = injectRefSet(checks.disallowedReferences),
            .allowedRequisites =
                checks.allowedRequisites ? std::optional{injectRefSet(*checks.allowedRequisites)} : std::nullopt,
            .disallowedRequisites = injectRefSet(checks.disallowedRequisites),
        };
    };

    for (auto & [outputName, output] : outputs) {
        auto & resOutput = res.outputs.at(outputName);
        resOutput.options.unsafeDiscardReferences = output.options.unsafeDiscardReferences;
        if (output.options.checks)
            resOutput.options.checks = injectChecks(*output.options.checks);
    }

    if (options.allOutputChecks)
        res.options.allOutputChecks = injectChecks(*options.allOutputChecks);
    for (auto & [name, paths] : options.exportReferencesGraph) {
        std::set<SingleDerivedPath> injected;
        for (auto & p : paths)
            injected.insert(SingleDerivedPath::Opaque{p});
        res.options.exportReferencesGraph.insert_or_assign(name, std::move(injected));
    }
    res.options.additionalSandboxProfile = options.additionalSandboxProfile;
    res.options.noChroot = options.noChroot;
    res.options.impureHostDeps = options.impureHostDeps;
    res.options.impureEnvVars = options.impureEnvVars;
    res.options.allowLocalNetworking = options.allowLocalNetworking;
    res.options.requiredSystemFeatures = options.requiredSystemFeatures;
    res.options.preferLocalBuild = options.preferLocalBuild;
    res.options.allowSubstitutes = options.allowSubstitutes;

    return res;
}

template<>
/**
 * Does the derivation have a dependency on the output of a dynamic
 * derivation?
 *
 * In other words, does it on the output of derivation that is itself an
 * output of a derivation? This corresponds to a dependency that is an
 * inductive derived path with more than one layer of
 * `DerivedPath::Built`.
 */
static bool hasDynamicDrvDep(const std::set<SingleDerivedPath> & inputs)
{
    return std::ranges::any_of(inputs, [](const auto & input) {
        if (auto * built = std::get_if<SingleDerivedPath::Built>(&input.raw()))
            return std::holds_alternative<SingleDerivedPath::Built>(built->drvPath->raw());
        return false;
    });
}

bool Derivation::shouldResolve() const
{
    bool hasInputDrvs = std::ranges::any_of(
        inputs, [](const auto & input) { return std::holds_alternative<SingleDerivedPath::Built>(input.raw()); });

    /* No input drvs means nothing to resolve. */
    if (!hasInputDrvs)
        return false;

    auto drvType = type();

    bool typeNeedsResolve = std::visit(
        overloaded{
            [&](const DerivationType::InputAddressed & ia) {
                /* Must resolve if deferred. */
                return ia.deferred;
            },
            [&](const DerivationType::ContentAddressed & ca) {
                return ca.fixed
                           /* Can optionally resolve if fixed, which is good
                              for avoiding unnecessary rebuilds. */
                           ? experimentalFeatureSettings.isEnabled(Xp::CaDerivations)
                           /* Must resolve if floating. */
                           : true;
            },
            [&](const DerivationType::Impure &) { return true; },
        },
        drvType.raw);

    return typeNeedsResolve ||
           /* Also need to resolve if any inputs are outputs of dynamic derivations. */
           hasDynamicDrvDep(inputs);
}

template<bool fillIn>
static void processDerivationOutputPaths(Store & store, auto && drv, std::string_view drvName);

// Forward declaration of specialization
template<>
std::optional<BasicDerivation> Derivation::tryResolve(
    Store & store,
    fun<std::optional<StorePath>(ref<const SingleDerivedPath> drvPath, const std::string & outputName)>
        queryResolutionChain) const;

template<>
std::optional<BasicDerivation> Derivation::tryResolve(Store & store, Store * evalStore) const
{
    return tryResolve(
        store, [&](ref<const SingleDerivedPath> drvPath, const std::string & outputName) -> std::optional<StorePath> {
            try {
                return resolveDerivedPath(store, SingleDerivedPath::Built{drvPath, outputName}, evalStore);
            } catch (Error &) {
                return std::nullopt;
            }
        });
}

template<>
std::optional<BasicDerivation> Derivation::tryResolve(
    Store & store,
    fun<std::optional<StorePath>(ref<const SingleDerivedPath> drvPath, const std::string & outputName)>
        queryResolutionChain) const
{
    StorePathSet resolvedInputs;
    StringMap inputRewrites;

    for (const auto & input : inputs) {
        auto resolved = std::visit(
            overloaded{
                [&](const SingleDerivedPath::Opaque & op) -> std::optional<StorePath> { return op.path; },
                [&](const SingleDerivedPath::Built & built) -> std::optional<StorePath> {
                    auto actualPathOpt = queryResolutionChain(built.drvPath, built.output);
                    if (!actualPathOpt)
                        return std::nullopt;

                    if (experimentalFeatureSettings.isEnabled(Xp::CaDerivations)) {
                        auto * p = std::get_if<SingleDerivedPath::Opaque>(&built.drvPath->raw());
                        if (p) {
                            auto placeholder = DownstreamPlaceholder::unknownCaOutput(p->path, built.output);
                            inputRewrites.emplace(placeholder.render(), store.printStorePath(*actualPathOpt));
                        }
                    }

                    return actualPathOpt;
                },
            },
            input.raw());

        if (!resolved)
            return std::nullopt;
        resolvedInputs.insert(*resolved);
    }

    BasicDerivation result{
        .name = name,
        .inputs = resolvedInputs,
        .platform = platform,
        .builder = builder,
        .args = args,
        .env = env,
        .structuredAttrs = structuredAttrs,
    };
    for (auto & [outputName, output] : outputs)
        result.outputs.insert_or_assign(
            outputName,
            derivation::OutputWithOptions<StorePath, DerivationOutput>{
                /* The per-output options are resolved by
                   `tryResolveDerivationOptions` below. */
                .output = output.output,
            });

    result.applyRewrites(inputRewrites);

    processDerivationOutputPaths</*maskOuputs=*/true>(store, result, result.name);

    if (!tryResolveDerivationOptions(*this, result, queryResolutionChain))
        return std::nullopt;

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
            // Note that we do *not* recur with `fillIn`
            if constexpr (std::is_same_v<std::decay_t<decltype(drv)>, Derivation>) {
                hashModulo_ = hashDerivationModulo(store, drv);
            } else {
                hashModulo_ = hashDerivationModulo(store, drv.unresolve());
            }
        }
        return *hashModulo_;
    };

    for (auto & [outputName, output] : drv.outputs) {
        auto envHasRightPath = [&](const StorePath & actual, bool isDeferred = false) {
            if constexpr (fillIn) {
                auto j = drv.env.find(outputName);
                /* Fill in mode: fill in missing or empty environment
                   variables */
                if (j == drv.env.end())
                    drv.env.insert(j, {outputName, {.value = store.printStorePath(actual)}});
                else if (j->second.value == "")
                    j->second.value = store.printStorePath(actual);
                /* We know validation will succeed after fill-in, but
                   just to be extra sure, validate unconditionally */
            }
            auto j = drv.env.find(outputName);
            if (j == drv.env.end())
                throw Error(
                    "derivation has missing environment variable '%s', should be '%s' but is not present",
                    outputName,
                    store.printStorePath(actual));
            if (j->second.value != store.printStorePath(actual)) {
                if (isDeferred)
                    warn(
                        "derivation has incorrect environment variable '%s', should be '%s' but is actually '%s'\nThis will be an error in future versions of Nix; compatibility of CA derivations will be broken.",
                        outputName,
                        store.printStorePath(actual),
                        j->second.value);
                else
                    throw Error(
                        "derivation has incorrect environment variable '%s', should be '%s' but is actually '%s'",
                        outputName,
                        store.printStorePath(actual),
                        j->second.value);
            }
        };
        auto hash = [&]<typename Output>(const Output & outputVariant) {
            auto & drvHash = hashModulo();
            if (drvHash) {
                auto outPath = store.makeOutputPath(outputName, *drvHash, drvName);

                if constexpr (std::is_same_v<Output, DerivationOutput::InputAddressed>) {
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
                } else if constexpr (std::is_same_v<Output, DerivationOutput::Deferred>) {
                    if constexpr (fillIn) {
                        /* Fill in output path for Deferred outputs */
                        output.output = DerivationOutput::InputAddressed{
                            .path = outPath,
                        };
                        envHasRightPath(outPath);
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
                /* Deferred — hash not yet known. */
                if constexpr (std::is_same_v<Output, DerivationOutput::InputAddressed>) {
                    /* Error case, an explicitly wrong path is
                       always an error. */
                    throw Error(
                        "derivation has incorrect output '%s', should be deferred",
                        store.printStorePath(outputVariant.path));
                } else if constexpr (std::is_same_v<Output, DerivationOutput::Deferred>) {
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
                [&](const DerivationOutput::InputAddressed & o) { hash(o); },
                [&](const DerivationOutput::Deferred & o) { hash(o); },
                [&](const DerivationOutput::CAFixed & dof) { envHasRightPath(dof.path(store, drvName, outputName)); },
                [&](const auto &) {
                    // Nothing to do for other output types
                },
            },
            output.output.raw);
    }

    /* Don't need the answer, but do this anyways to assert is proper
       combination. The code above is more general and naturally allows
       combinations that are currently prohibited. */
    drv.type();
}

template<typename Inputs, typename Output>
void DerivationT<Inputs, Output>::checkInvariants(Store & store, const StorePath & drvPath) const
    requires std::is_same_v<Output, DerivationOutput>
{
    assert(drvPath.isDerivation());
    std::string drvName(drvPath.name());
    drvName = drvName.substr(0, drvName.size() - drvExtension.size());

    if (drvName != name) {
        throw Error("derivation '%s' has name '%s' which does not match its path", store.printStorePath(drvPath), name);
    }

    try {
        checkInvariants(store);
    } catch (Error & e) {
        e.addTrace({}, "while checking derivation '%s'", store.printStorePath(drvPath));
        throw;
    }
}

template<>
void BasicDerivation::checkInvariants(Store & store) const
{
    processDerivationOutputPaths<false>(store, *this, name);
}

template<>
void Derivation::checkInvariants(Store & store) const
{
    processDerivationOutputPaths<false>(store, *this, name);
}

template<>
void Derivation::fillInOutputPaths(Store & store)
{
    processDerivationOutputPaths<true>(store, *this, name);
}

template<>
Derivation Derivation::parseJsonAndValidate(Store & store, const nlohmann::json & json)
{
    auto drv = static_cast<Derivation>(json);

    drv.fillInOutputPaths(store);

    try {
        drv.checkInvariants(store);
    } catch (Error & e) {
        e.addTrace({}, "while checking derivation from JSON with name '%s'", drv.name);
        throw;
    }

    return drv;
}

const Hash impureOutputHash = hashString(HashAlgorithm::SHA256, "impure");

// Explicit template instantiations
template struct DerivationT<StorePath>;
template struct DerivationT<SingleDerivedPath>;

} // namespace nix
