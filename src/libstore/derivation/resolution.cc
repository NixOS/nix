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

    fillInOutputPaths(result, store);

    return result;
}

} // namespace derivation

} // namespace nix
