#include "nix/store/build/derivation-resolution-goal.hh"
#include "nix/store/build/worker.hh"
#include "nix/util/util.hh"

#include <nlohmann/json.hpp>

namespace nix {

DerivationResolutionGoal::DerivationResolutionGoal(
    const StorePath & drvPath, const Derivation & drv, Worker & worker, BuildMode buildMode)
    : Goal(worker, resolveDerivation())
    , drvPath(drvPath)
    , drv{std::make_unique<Derivation>(drv)}
    , buildMode{buildMode}
{
    name = fmt("resolving derivation '%s'", worker.store.printStorePath(drvPath));
    trace("created");
}

std::string DerivationResolutionGoal::key()
{
    return "dc$" + std::string(drvPath.name()) + "$" + worker.store.printStorePath(drvPath);
}

/**
 * Used for `inputGoals` local variable below
 */
struct value_comparison
{
    template<typename T>
    bool operator()(const ref<T> & lhs, const ref<T> & rhs) const
    {
        return *lhs < *rhs;
    }
};

Goal::Co DerivationResolutionGoal::resolveDerivation()
{
    Goals waitees;

    std::map<ref<const SingleDerivedPath>, GoalPtr, value_comparison> inputGoals;

    {
        std::function<void(ref<const SingleDerivedPath>, const DerivedPathMap<StringSet>::ChildNode &)>
            addWaiteeDerivedPath;

        addWaiteeDerivedPath = [&](ref<const SingleDerivedPath> inputDrv,
                                   const DerivedPathMap<StringSet>::ChildNode & inputNode) {
            if (!inputNode.value.empty()) {
                auto g = worker.makeGoal(
                    DerivedPath::Built{
                        .drvPath = inputDrv,
                        .outputs = inputNode.value,
                    },
                    buildMode == bmRepair ? bmRepair : bmNormal);
                inputGoals.insert_or_assign(inputDrv, g);
                waitees.insert(std::move(g));
            }
            for (const auto & [outputName, childNode] : inputNode.childMap)
                addWaiteeDerivedPath(
                    make_ref<SingleDerivedPath>(SingleDerivedPath::Built{inputDrv, outputName}), childNode);
        };

        for (const auto & [inputDrvPath, inputNode] : drv->inputDrvs.map) {
            /* Ensure that pure, non-fixed-output derivations don't
               depend on impure derivations. */
            if (experimentalFeatureSettings.isEnabled(Xp::ImpureDerivations) && !drv->type().isImpure()
                && !drv->type().isFixed()) {
                auto inputDrv = worker.evalStore.readDerivation(inputDrvPath);
                if (inputDrv.type().isImpure())
                    throw Error(
                        "pure derivation '%s' depends on impure derivation '%s'",
                        worker.store.printStorePath(drvPath),
                        worker.store.printStorePath(inputDrvPath));
            }

            addWaiteeDerivedPath(makeConstantStorePathRef(inputDrvPath), inputNode);
        }
    }

    co_await await(std::move(waitees));

    trace("all inputs realised");

    if (nrFailed != 0) {
        auto msg =
            fmt("Cannot build '%s'.\n"
                "Reason: " ANSI_RED "%d %s failed" ANSI_NORMAL ".",
                Magenta(worker.store.printStorePath(drvPath)),
                nrFailed,
                nrFailed == 1 ? "dependency" : "dependencies");
        msg += showKnownOutputs(worker.store, *drv);
        co_return amDone(ecFailed, {BuildError(BuildResult::Failure::DependencyFailed, msg)});
    }

    /* Gather information necessary for computing the closure and/or
       running the build hook. */

    /* Determine the full set of input paths. */

    /* First, the input derivations. */
    {
        auto & fullDrv = *drv;

        auto drvType = fullDrv.type();
        bool resolveDrv =
            std::visit(
                overloaded{
                    [&](const DerivationType::InputAddressed & ia) {
                        /* must resolve if deferred. */
                        return ia.deferred;
                    },
                    [&](const DerivationType::ContentAddressed & ca) {
                        return !fullDrv.inputDrvs.map.empty()
                               && (ca.fixed
                                       /* Can optionally resolve if fixed, which is good
                                          for avoiding unnecessary rebuilds. */
                                       ? experimentalFeatureSettings.isEnabled(Xp::CaDerivations)
                                       /* Must resolve if floating and there are any inputs
                                          drvs. */
                                       : true);
                    },
                    [&](const DerivationType::Impure &) { return true; }},
                drvType.raw)
            /* no inputs are outputs of dynamic derivations */
            || std::ranges::any_of(fullDrv.inputDrvs.map.begin(), fullDrv.inputDrvs.map.end(), [](auto & pair) {
                   return !pair.second.childMap.empty();
               });

        if (resolveDrv && !fullDrv.inputDrvs.map.empty()) {
            experimentalFeatureSettings.require(Xp::CaDerivations);

            /* We are be able to resolve this derivation based on the
               now-known results of dependencies. If so, we become a
               stub goal aliasing that resolved derivation goal. */
            std::optional attempt = fullDrv.tryResolve(
                worker.store,
                [&](ref<const SingleDerivedPath> drvPath, const std::string & outputName) -> std::optional<StorePath> {
                    auto mEntry = get(inputGoals, drvPath);
                    if (!mEntry)
                        return std::nullopt;

                    auto & buildResult = (*mEntry)->buildResult;
                    return std::visit(
                        overloaded{
                            [](const BuildResult::Failure &) -> std::optional<StorePath> { return std::nullopt; },
                            [&](const BuildResult::Success & success) -> std::optional<StorePath> {
                                auto i = get(success.builtOutputs, outputName);
                                if (!i)
                                    return std::nullopt;

                                return i->outPath;
                            },
                        },
                        buildResult.inner);
                });
            if (!attempt) {
                /* TODO (impure derivations-induced tech debt) (see below):
                   The above attempt should have found it, but because we manage
                   inputDrvOutputs statefully, sometimes it gets out of sync with
                   the real source of truth (store). So we query the store
                   directly if there's a problem. */
                attempt = fullDrv.tryResolve(worker.store, &worker.evalStore);
            }
            assert(attempt);

            auto pathResolved = writeDerivation(worker.store, *attempt, NoRepair, /*readOnly =*/true);

            auto msg =
                fmt("resolved derivation: '%s' -> '%s'",
                    worker.store.printStorePath(drvPath),
                    worker.store.printStorePath(pathResolved));
            act = std::make_unique<Activity>(
                *logger,
                lvlInfo,
                actBuildWaiting,
                msg,
                Logger::Fields{
                    worker.store.printStorePath(drvPath),
                    worker.store.printStorePath(pathResolved),
                });

            resolvedDrv =
                std::make_unique<std::pair<StorePath, BasicDerivation>>(std::move(pathResolved), *std::move(attempt));
        }
    }

    co_return amDone(ecSuccess, std::nullopt);
}

} // namespace nix
