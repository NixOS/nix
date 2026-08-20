#include "nix/store/derivations.hh"
#include "nix/store/derivation/resolution.hh"
#include "nix/store/downstream-placeholder.hh"
#include "nix/store/store-api.hh"

#include <algorithm>
#include <optional>
#include <ranges>

namespace nix {

namespace derivation {

Full unresolve(const Basic & drv)
{
    auto res = drv.mapInputs([](const StorePathSet & inputs) -> std::set<SingleDerivedPath> {
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

    for (auto & [outputName, output] : drv.outputs) {
        auto & resOutput = res.outputs.at(outputName);
        resOutput.options.unsafeDiscardReferences = output.options.unsafeDiscardReferences;
        if (output.options.checks)
            resOutput.options.checks = injectChecks(*output.options.checks);
    }

    if (drv.options.allOutputChecks)
        res.options.allOutputChecks = injectChecks(*drv.options.allOutputChecks);
    for (auto & [name, paths] : drv.options.exportReferencesGraph) {
        std::set<SingleDerivedPath> injected;
        for (auto & p : paths)
            injected.insert(SingleDerivedPath::Opaque{p});
        res.options.exportReferencesGraph.insert_or_assign(name, std::move(injected));
    }
    res.options.additionalSandboxProfile = drv.options.additionalSandboxProfile;
    res.options.noChroot = drv.options.noChroot;
    res.options.impureHostDeps = drv.options.impureHostDeps;
    res.options.impureEnvVars = drv.options.impureEnvVars;
    res.options.allowLocalNetworking = drv.options.allowLocalNetworking;
    res.options.requiredSystemFeatures = drv.options.requiredSystemFeatures;
    res.options.preferLocalBuild = drv.options.preferLocalBuild;
    res.options.allowSubstitutes = drv.options.allowSubstitutes;

    return res;
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
        .name = drv.name,
        .inputs = resolvedInputs,
        .platform = drv.platform,
        .builder = drv.builder,
        .args = drv.args,
        .env = drv.env,
        .structuredAttrs = drv.structuredAttrs,
    };
    for (auto & [outputName, output] : drv.outputs)
        result.outputs.insert_or_assign(
            outputName,
            OutputWithOptions<StorePath, Output>{
                /* The per-output options are resolved by
                   `tryResolveDerivationOptions` below. */
                .output = output.output,
            });

    result.applyRewrites(inputRewrites);

    fillInOutputPaths(result, store);

    if (!tryResolveDerivationOptions(drv, result, queryResolutionChain))
        return std::nullopt;

    return result;
}

} // namespace derivation

bool tryResolveDerivationOptions(
    const Derivation & drvOptions,
    BasicDerivation & resolved,
    fun<std::optional<StorePath>(ref<const SingleDerivedPath> drvPath, const std::string & outputName)>
        queryResolutionChain)
{
    auto tryResolvePath = [&](const SingleDerivedPath & input) -> std::optional<StorePath> {
        return std::visit(
            overloaded{
                [](const SingleDerivedPath::Opaque & p) -> std::optional<StorePath> { return p.path; },
                [&](const SingleDerivedPath::Built & p) -> std::optional<StorePath> {
                    return queryResolutionChain(p.drvPath, p.output);
                }},
            input.raw());
    };

    auto tryResolveRef = [&](const DrvRef<SingleDerivedPath> & ref) -> std::optional<DrvRef<StorePath>> {
        return std::visit(
            overloaded{
                [](const OutputName & outputName) -> std::optional<DrvRef<StorePath>> { return outputName; },
                [&](const SingleDerivedPath & input) -> std::optional<DrvRef<StorePath>> {
                    return tryResolvePath(input);
                }},
            ref);
    };

    auto tryResolveRefSet =
        [&](const std::set<DrvRef<SingleDerivedPath>> & refSet) -> std::optional<std::set<DrvRef<StorePath>>> {
        std::set<DrvRef<StorePath>> resolvedSet;
        for (const auto & ref : refSet) {
            auto resolvedRef = tryResolveRef(ref);
            if (!resolvedRef)
                return std::nullopt;
            resolvedSet.insert(*resolvedRef);
        }
        return resolvedSet;
    };

    // Helper function to try resolving OutputChecks using functional style
    auto tryResolveOutputChecks = [&](const derivation::OutputChecks<SingleDerivedPath> & checks)
        -> std::optional<derivation::OutputChecks<StorePath>> {
        std::optional<std::set<DrvRef<StorePath>>> resolvedAllowedReferences;
        if (checks.allowedReferences) {
            resolvedAllowedReferences = tryResolveRefSet(*checks.allowedReferences);
            if (!resolvedAllowedReferences)
                return std::nullopt;
        }

        std::optional<std::set<DrvRef<StorePath>>> resolvedAllowedRequisites;
        if (checks.allowedRequisites) {
            resolvedAllowedRequisites = tryResolveRefSet(*checks.allowedRequisites);
            if (!resolvedAllowedRequisites)
                return std::nullopt;
        }

        auto resolvedDisallowedReferences = tryResolveRefSet(checks.disallowedReferences);
        if (!resolvedDisallowedReferences)
            return std::nullopt;

        auto resolvedDisallowedRequisites = tryResolveRefSet(checks.disallowedRequisites);
        if (!resolvedDisallowedRequisites)
            return std::nullopt;

        return derivation::OutputChecks<StorePath>{
            .ignoreSelfRefs = checks.ignoreSelfRefs,
            .maxSize = checks.maxSize,
            .maxClosureSize = checks.maxClosureSize,
            .allowedReferences = resolvedAllowedReferences,
            .disallowedReferences = *resolvedDisallowedReferences,
            .allowedRequisites = resolvedAllowedRequisites,
            .disallowedRequisites = *resolvedDisallowedRequisites,
        };
    };

    // Helper function to resolve exportReferencesGraph using functional style
    auto tryResolveExportReferencesGraph =
        [&](const std::map<std::string, std::set<SingleDerivedPath>, std::less<>> & exportGraph)
        -> std::optional<std::map<std::string, std::set<StorePath>, std::less<>>> {
        std::map<std::string, std::set<StorePath>, std::less<>> resolved;
        for (const auto & [name, inputPaths] : exportGraph) {
            std::set<StorePath> resolvedPaths;
            for (const auto & inputPath : inputPaths) {
                auto resolvedPath = tryResolvePath(inputPath);
                if (!resolvedPath)
                    return std::nullopt;
                resolvedPaths.insert(*resolvedPath);
            }
            resolved.emplace(name, std::move(resolvedPaths));
        }
        return resolved;
    };

    // Resolve the all-outputs checks, if any
    std::optional<derivation::OutputChecks<StorePath>> resolvedAllChecks;
    if (drvOptions.options.allOutputChecks) {
        resolvedAllChecks = tryResolveOutputChecks(*drvOptions.options.allOutputChecks);
        if (!resolvedAllChecks)
            return false;
    }

    // Resolve the per-output options; `resolved.outputs` is expected to
    // already contain the (resolved) outputs under the same names.
    for (const auto & [outputName, output] : drvOptions.outputs) {
        auto it = resolved.outputs.find(outputName);
        if (it == resolved.outputs.end())
            continue;
        it->second.options.unsafeDiscardReferences = output.options.unsafeDiscardReferences;
        if (output.options.checks) {
            auto resolvedChecks = tryResolveOutputChecks(*output.options.checks);
            if (!resolvedChecks)
                return false;
            it->second.options.checks = std::move(*resolvedChecks);
        }
    }

    // Resolve exportReferencesGraph
    auto resolvedExportGraph = tryResolveExportReferencesGraph(drvOptions.options.exportReferencesGraph);
    if (!resolvedExportGraph)
        return false;

    resolved.options.allOutputChecks = std::move(resolvedAllChecks);
    resolved.options.exportReferencesGraph = std::move(*resolvedExportGraph);
    resolved.options.additionalSandboxProfile = drvOptions.options.additionalSandboxProfile;
    resolved.options.noChroot = drvOptions.options.noChroot;
    resolved.options.impureHostDeps = drvOptions.options.impureHostDeps;
    resolved.options.impureEnvVars = drvOptions.options.impureEnvVars;
    resolved.options.allowLocalNetworking = drvOptions.options.allowLocalNetworking;
    resolved.options.requiredSystemFeatures = drvOptions.options.requiredSystemFeatures;
    resolved.options.preferLocalBuild = drvOptions.options.preferLocalBuild;
    resolved.options.allowSubstitutes = drvOptions.options.allowSubstitutes;
    return true;
}

} // namespace nix
