#include "nix/store/build/derivation-resolution-goal.hh"
#include "nix/store/build/derivation-env-desugar.hh"
#include "nix/store/build/worker.hh"
#include "nix/util/util.hh"
#include "nix/store/common-protocol.hh"
#include "nix/store/globals.hh"

#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>

#include <nlohmann/json.hpp>

namespace nix {

DerivationResolutionGoal::DerivationResolutionGoal(
    const StorePath & drvPath, const Derivation & drv_, Worker & worker, BuildMode buildMode)
    : Goal(worker, gaveUpOnSubstitution())
    , drvPath(drvPath)
{
    drv = std::make_unique<Derivation>(drv_);

    name = fmt("building of '%s' from in-memory derivation", worker.store.printStorePath(drvPath));
    trace("created");

    /* Prevent the .chroot directory from being
       garbage-collected. (See isActiveTempFile() in gc.cc.) */
    worker.store.addTempRoot(this->drvPath);
}

void DerivationResolutionGoal::timedOut(Error && ex) {}

std::string DerivationResolutionGoal::key()
{
    /* Ensure that derivations get built in order of their name,
       i.e. a derivation named "aardvark" always comes before
       "baboon". And substitution goals always happen before
       derivation goals (due to "bd$"). */
    return "rd$" + std::string(drvPath.name()) + "$" + worker.store.printStorePath(drvPath);
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

/* At least one of the output paths could not be
   produced using a substitute.  So we have to build instead. */
Goal::Co DerivationResolutionGoal::gaveUpOnSubstitution()
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

    /* Copy the input sources from the eval store to the build
       store.

       Note that some inputs might not be in the eval store because they
       are (resolved) derivation outputs in a resolved derivation. */
    if (&worker.evalStore != &worker.store) {
        RealisedPath::Set inputSrcs;
        for (auto & i : drv->inputSrcs)
            if (worker.evalStore.isValidPath(i))
                inputSrcs.insert(i);
        copyClosure(worker.evalStore, worker.store, inputSrcs);
    }

    for (auto & i : drv->inputSrcs) {
        if (worker.store.isValidPath(i))
            continue;
        if (!settings.useSubstitutes)
            throw Error(
                "dependency '%s' of '%s' does not exist, and substitution is disabled",
                worker.store.printStorePath(i),
                worker.store.printStorePath(drvPath));
        waitees.insert(upcast_goal(worker.makePathSubstitutionGoal(i)));
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
        co_return amDone(ecFailed, {BuildError(BuildResult::DependencyFailed, msg)});
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
                    if (!buildResult.success())
                        return std::nullopt;

                    auto i = get(buildResult.builtOutputs, outputName);
                    if (!i)
                        return std::nullopt;

                    return i->outPath;
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
            resolvedDrv = std::make_unique<BasicDerivation>(*std::move(attempt));

            auto pathResolved = writeDerivation(worker.store, *resolvedDrv, NoRepair, /*readOnly =*/true);

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

            co_return amDone(ecSuccess, std::nullopt);
        }

        /* If we get this far, we know no dynamic drvs inputs */

        for (auto & [depDrvPath, depNode] : fullDrv.inputDrvs.map) {
            for (auto & outputName : depNode.value) {
                /* Don't need to worry about `inputGoals`, because
                   impure derivations are always resolved above. Can
                   just use DB. This case only happens in the (older)
                   input addressed and fixed output derivation cases. */
                auto outMap = [&] {
                    for (auto * drvStore : {&worker.evalStore, &worker.store})
                        if (drvStore->isValidPath(depDrvPath))
                            return worker.store.queryDerivationOutputMap(depDrvPath, drvStore);
                    assert(false);
                }();

                auto outMapPath = outMap.find(outputName);
                if (outMapPath == outMap.end()) {
                    throw Error(
                        "derivation '%s' requires non-existent output '%s' from input derivation '%s'",
                        worker.store.printStorePath(drvPath),
                        outputName,
                        worker.store.printStorePath(depDrvPath));
                }

                worker.store.computeFSClosure(outMapPath->second, inputPaths);
            }
        }
    }

    /* Second, the input sources. */
    worker.store.computeFSClosure(drv->inputSrcs, inputPaths);

    debug("added input paths %s", worker.store.showPaths(inputPaths));

    /* Okay, try to build.  Note that here we don't wait for a build
       slot to become available, since we don't need one if there is a
       build hook. */
    co_await yield();
    co_return amDone(ecSuccess, std::nullopt);
}

} // namespace nix
