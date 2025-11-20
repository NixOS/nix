#include "nix/store/build/derivation-resolution-goal.hh"
#include "nix/store/build/worker.hh"
#include "nix/util/util.hh"

#include <nlohmann/json.hpp>

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

    for (const auto & input : drv->inputs) {
        std::visit(
            overloaded{
                [&](const SingleDerivedPath::Opaque &) {
                    /* Source paths don't need to be waited on */
                },
                [&](const SingleDerivedPath::Built & built) {
                    /* Ensure that pure, non-fixed-output derivations don't
                       depend on impure derivations. */
                    if (experimentalFeatureSettings.isEnabled(Xp::ImpureDerivations) && !drv->type().isImpure()
                        && !drv->type().isFixed()) {
                        auto inputDrvPath = std::visit(
                            overloaded{
                                [&](const SingleDerivedPath::Opaque & op) { return op.path; },
                                [&](const SingleDerivedPath::Built &) -> StorePath {
                                    /* Dynamic derivation - for now just skip the check */
                                    return StorePath::dummy;
                                }},
                            built.drvPath->raw());
                        if (inputDrvPath != StorePath::dummy) {
                            auto inputDrv = worker.evalStore.readDerivation(inputDrvPath);
                            if (inputDrv.type().isImpure())
                                throw Error(
                                    "pure derivation '%s' depends on impure derivation '%s'",
                                    worker.store.printStorePath(drvPath),
                                    worker.store.printStorePath(inputDrvPath));
                        }
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

    auto drvType = fullDrv.type();

    /* Check if we have any derivation inputs */
    bool hasInputDrvs = std::ranges::any_of(fullDrv.inputs, [](const auto & input) {
        return std::holds_alternative<SingleDerivedPath::Built>(input.raw());
    });

    /* Check if any inputs are outputs of dynamic derivations */
    bool hasDynamicDrvInputs = std::ranges::any_of(fullDrv.inputs, [](const auto & input) {
        if (auto * built = std::get_if<SingleDerivedPath::Built>(&input.raw())) {
            return std::holds_alternative<SingleDerivedPath::Built>(built->drvPath->raw());
        }
        return false;
    });

    bool resolveDrv = std::visit(
                          overloaded{
                              [&](const DerivationType::InputAddressed & ia) {
                                  /* must resolve if deferred. */
                                  return ia.deferred;
                              },
                              [&](const DerivationType::ContentAddressed & ca) {
                                  return hasInputDrvs
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
                      || hasDynamicDrvInputs;

    if (resolveDrv && hasInputDrvs) {
        experimentalFeatureSettings.require(Xp::CaDerivations);

        /* We are be able to resolve this derivation based on the
           now-known results of dependencies. If so, we become a
           stub goal aliasing that resolved derivation goal. */

        auto attempt = fullDrv.tryResolve(
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
            co_return doneFailure(
                ecFailed, BuildError(BuildResult::Failure::DependencyFailed, "failed to resolve derivation"));
        }

        auto pathResolved = computeStorePath(worker.store, attempt->unresolve());

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
            std::make_unique<std::pair<StorePath, BasicDerivation>>(std::move(pathResolved), std::move(*attempt));
    }

    co_return amDone(ecSuccess);
}

} // namespace nix
