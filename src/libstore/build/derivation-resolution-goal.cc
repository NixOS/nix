#include "nix/store/build/derivation-resolution-goal.hh"
#include "nix/store/build/worker.hh"
#include "nix/util/util.hh"
#include "nix/store/derivation/resolution.hh"

#include <nlohmann/json.hpp>

#include <array>

namespace nix {

DerivationResolutionGoal::DerivationResolutionGoal(
    const StorePath & drvPath, ref<const Derivation> drv, Worker & worker, BuildMode buildMode)
    : Goal(worker, resolveDerivation())
    , drvPath(drvPath)
    , drv(std::move(drv))
    , buildMode{buildMode}
{
    name = fmt("resolving derivation '%s'", worker.store.printStorePath(drvPath));
    trace("created");
}

std::string DerivationResolutionGoal::key()
{
    return "dc$" + std::string(drvPath.name()) + "$" + worker.store.printStorePath(drvPath);
}

Goal::Co DerivationResolutionGoal::resolveDerivation()
{
    Goals waitees;

    using ValueComparison = decltype([]<typename T>(const ref<T> & lhs, const ref<T> & rhs) {
        /* Compare the values, not the pointers themselves. */
        return *lhs < *rhs;
    });

    std::map<ref<const SingleDerivedPath>, GoalPtr, ValueComparison> inputGoals;

    /* Ensure that pure, non-fixed-output derivations don't depend on
       impure derivations. Only worth checking each input derivation
       once, however many of its outputs we depend on. */
    bool checkImpureInputs =
        experimentalFeatureSettings.isEnabled(Xp::ImpureDerivations) && !type(*drv).isImpure() && !type(*drv).isFixed();
    StorePathSet checkedInputDrvs;

    for (const auto & input : drv->inputs) {
        std::visit(
            overloaded{
                [&](const SingleDerivedPath::Opaque &) {
                    /* Source paths don't need to be waited on */
                },
                [&](const SingleDerivedPath::Built & built) {
                    /* For a dynamic derivation input, this checks the
                       root derivation the chain is ultimately built
                       from. */
                    auto & inputDrvPath = built.drvPath->getBaseStorePath();
                    if (checkImpureInputs && checkedInputDrvs.insert(inputDrvPath).second) {
                        auto inputDrv = worker.evalStore.readDerivation(inputDrvPath);
                        if (type(inputDrv).isImpure())
                            throw Error(
                                "pure derivation '%s' depends on impure derivation '%s'",
                                worker.store.printStorePath(drvPath),
                                worker.store.printStorePath(inputDrvPath));
                    }

                    auto inputDrvRef = make_ref<SingleDerivedPath>(input);
                    auto g = worker.makeGoal(
                        DerivedPath::Built{
                            .drvPath = built.drvPath,
                            .outputs = OutputsSpec::Names{built.output},
                        },
                        buildMode == bmRepair ? bmRepair : bmNormal);
                    inputGoals.insert_or_assign(inputDrvRef, g);
                    waitees.insert(std::move(g));
                }},
            input.raw());
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
    auto & fullDrv = *drv;

    if (derivation::shouldResolve(fullDrv)) {
        experimentalFeatureSettings.require(Xp::CaDerivations);

        /* We are be able to resolve this derivation based on the
           now-known results of dependencies. If so, we become a
           stub goal aliasing that resolved derivation goal. */

        auto attempt = tryResolve(
            fullDrv,
            worker.store,
            [&](ref<const SingleDerivedPath> inputDrvPath, const std::string & outputName) -> std::optional<StorePath> {
                auto inputDrvRef = make_ref<SingleDerivedPath>(SingleDerivedPath::Built{inputDrvPath, outputName});
                auto mEntry = get(inputGoals, inputDrvRef);
                if (!mEntry)
                    return std::nullopt;
                auto & buildResult = (*mEntry)->buildResult;
                return std::visit(
                    overloaded{
                        [](const BuildResult::Failure &) -> std::optional<StorePath> { return std::nullopt; },
                        [&](const BuildResult::Success & success) -> std::optional<StorePath> {
                            auto i = get(success.builtOutputs, outputName);
                            if (i)
                                return i->outPath;
                            return std::nullopt;
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
            attempt = tryResolve(fullDrv, worker.store, &worker.evalStore);
        }
        assert(attempt);

        auto pathResolved = computeStorePath(worker.store, unresolve(*attempt));

        auto msg =
            fmt("resolved derivation: '%s' -> '%s'",
                worker.store.printStorePath(drvPath),
                worker.store.printStorePath(pathResolved));
        act = std::make_unique<Activity>(
            *logger,
            lvlInfo,
            actBuildWaiting,
            msg,
            std::to_array<Logger::Field>({
                worker.store.printStorePath(drvPath),
                worker.store.printStorePath(pathResolved),
            }));

        resolvedDrv =
            std::make_unique<std::pair<StorePath, BasicDerivation>>(std::move(pathResolved), std::move(*attempt));
    }

    co_return amDone(ecSuccess);
}

} // namespace nix
