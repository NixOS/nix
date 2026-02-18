#include "nix/store/build/derivation-resolution-goal.hh"
#include "nix/store/build/derived-output-goal.hh"
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

    /**
     * Map from output deriving path to the DerivedOutputGoal
     * that will get its realisation (either from build trace lookup or by building).
     */
    std::map<SingleDerivedPath::Built, std::shared_ptr<DerivedOutputGoal>> inputGoals;

    {
        std::function<void(ref<const SingleDerivedPath>, const DerivedPathMap<StringSet>::ChildNode &)>
            addWaiteeDerivedPath;

        addWaiteeDerivedPath = [&](ref<const SingleDerivedPath> inputDrv,
                                   const DerivedPathMap<StringSet>::ChildNode & inputNode) {
            for (const auto & outputName : inputNode.value) {
                SingleDerivedPath::Built id{inputDrv, outputName};
                auto g = worker.makeDerivedOutputGoal(id, buildMode == bmRepair ? bmRepair : bmNormal);
                inputGoals.insert_or_assign(std::move(id), g);
                waitees.insert(upcast_goal(g));
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
        co_return doneFailure(
            ecFailed,
            BuildResult::Failure{{
                .status = BuildResult::Failure::DependencyFailed,
                .msg = HintFmt(msg),
            }});
    }

    /* Gather information necessary for computing the closure and/or
       running the build hook. */

    /* Determine the full set of input paths. */

    /* First, the input derivations. */
    {
        auto & fullDrv = *drv;

        if (fullDrv.shouldResolve()) {
            experimentalFeatureSettings.require(Xp::CaDerivations);

            /* We are be able to resolve this derivation based on the
               now-known results of dependencies. If so, we become a
               stub goal aliasing that resolved derivation goal. */
            std::optional attempt = fullDrv.tryResolve(
                worker.store,
                [&](ref<const SingleDerivedPath> inputDrv, const std::string & outputName) -> std::optional<StorePath> {
                    auto mGoal = get(inputGoals, SingleDerivedPath::Built{inputDrv, outputName});
                    if (!mGoal)
                        return std::nullopt;

                    return (*mGoal)->outputPath;
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

            auto pathResolved = computeStorePath(worker.store, Derivation{*attempt});

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

    co_return amDone(ecSuccess);
}

} // namespace nix
