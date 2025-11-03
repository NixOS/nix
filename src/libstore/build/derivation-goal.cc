#include "nix/store/build/derivation-goal.hh"
#include "nix/store/build/derivation-building-goal.hh"
#include "nix/store/build/derivation-resolution-goal.hh"
#ifndef _WIN32 // TODO enable build hook on Windows
#  include "nix/store/build/hook-instance.hh"
#  include "nix/store/build/derivation-builder.hh"
#endif
#include "nix/util/processes.hh"
#include "nix/util/config-global.hh"
#include "nix/store/build/worker.hh"
#include "nix/util/util.hh"
#include "nix/util/compression.hh"
#include "nix/store/common-protocol.hh"
#include "nix/store/common-protocol-impl.hh" // Don't remove is actually needed
#include "nix/store/globals.hh"

#include <fstream>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>

#include <nlohmann/json.hpp>

#include "nix/util/strings.hh"

namespace nix {

DerivationGoal::DerivationGoal(
    const StorePath & drvPath,
    const Derivation & drv,
    const OutputName & wantedOutput,
    Worker & worker,
    BuildMode buildMode,
    bool storeDerivation)
    : Goal(worker, haveDerivation(storeDerivation))
    , drvPath(drvPath)
    , wantedOutput(wantedOutput)
    , drv{std::make_unique<Derivation>(drv)}
    , outputHash{[&] {
        auto outputHashes = staticOutputHashes(worker.evalStore, drv);
        if (auto * mOutputHash = get(outputHashes, wantedOutput))
            return *mOutputHash;
        throw Error("derivation '%s' does not have output '%s'", worker.store.printStorePath(drvPath), wantedOutput);
    }()}
    , buildMode(buildMode)
{

    name = fmt("getting output '%s' from derivation '%s'", wantedOutput, worker.store.printStorePath(drvPath));
    trace("created");

    mcExpectedBuilds = std::make_unique<MaintainCount<uint64_t>>(worker.expectedBuilds);
    worker.updateProgress();
}

std::string DerivationGoal::key()
{
    return "db$" + std::string(drvPath.name()) + "$" + SingleDerivedPath::Built{
        .drvPath = makeConstantStorePathRef(drvPath),
        .output = wantedOutput,
    }.to_string(worker.store);
}

Goal::Co DerivationGoal::haveDerivation(bool storeDerivation)
{
    trace("have derivation");

    auto drvOptions = [&]() -> DerivationOptions {
        try {
            return DerivationOptions::fromStructuredAttrs(drv->env, drv->structuredAttrs);
        } catch (Error & e) {
            e.addTrace({}, "while parsing derivation '%s'", worker.store.printStorePath(drvPath));
            throw;
        }
    }();

    if (!drv->type().hasKnownOutputPaths())
        experimentalFeatureSettings.require(Xp::CaDerivations);

    for (auto & i : drv->outputsAndOptPaths(worker.store))
        if (i.second.second)
            worker.store.addTempRoot(*i.second.second);

    /* We don't yet have any safe way to cache an impure derivation at
       this step. */
    if (drv->type().isImpure()) {
        experimentalFeatureSettings.require(Xp::ImpureDerivations);
    } else {
        /* Check what outputs paths are not already valid. */
        auto checkResult = checkPathValidity();

        /* If they are all valid, then we're done. */
        if (checkResult && checkResult->second == PathStatus::Valid && buildMode == bmNormal) {
            co_return doneSuccess(BuildResult::Success::AlreadyValid, checkResult->first);
        }

        Goals waitees;

        /* We are first going to try to create the invalid output paths
           through substitutes.  If that doesn't work, we'll build
           them. */
        if (settings.useSubstitutes && drvOptions.substitutesAllowed()) {
            if (!checkResult)
                waitees.insert(upcast_goal(worker.makeDrvOutputSubstitutionGoal(DrvOutput{outputHash, wantedOutput})));
            else {
                auto * cap = getDerivationCA(*drv);
                waitees.insert(upcast_goal(worker.makePathSubstitutionGoal(
                    checkResult->first.outPath,
                    buildMode == bmRepair ? Repair : NoRepair,
                    cap ? std::optional{*cap} : std::nullopt)));
            }
        }

        co_await await(std::move(waitees));

        trace("all outputs substituted (maybe)");

        assert(!drv->type().isImpure());

        if (nrFailed > 0 && nrFailed > nrNoSubstituters && !settings.tryFallback) {
            co_return doneFailure(BuildError(
                BuildResult::Failure::TransientFailure,
                "some substitutes for the outputs of derivation '%s' failed (usually happens due to networking issues); try '--fallback' to build derivation from source ",
                worker.store.printStorePath(drvPath)));
        }

        nrFailed = nrNoSubstituters = 0;

        checkResult = checkPathValidity();

        bool allValid = checkResult && checkResult->second == PathStatus::Valid;

        if (buildMode == bmNormal && allValid) {
            co_return doneSuccess(BuildResult::Success::Substituted, checkResult->first);
        }
        if (buildMode == bmRepair && allValid) {
            co_return repairClosure();
        }
        if (buildMode == bmCheck && !allValid)
            throw Error(
                "some outputs of '%s' are not valid, so checking is not possible",
                worker.store.printStorePath(drvPath));
    }

    auto resolutionGoal = worker.makeDerivationResolutionGoal(drvPath, *drv, buildMode);
    {
        Goals waitees{resolutionGoal};
        co_await await(std::move(waitees));
    }
    if (nrFailed != 0) {
        co_return doneFailure({BuildResult::Failure::DependencyFailed, "Build failed due to failed dependency"});
    }

    if (resolutionGoal->resolvedDrv) {
        auto & [pathResolved, drvResolved] = *resolutionGoal->resolvedDrv;

        auto resolvedDrvGoal =
            worker.makeDerivationGoal(pathResolved, drvResolved, wantedOutput, buildMode, /*storeDerivation=*/true);
        {
            Goals waitees{resolvedDrvGoal};
            co_await await(std::move(waitees));
        }

        trace("resolved derivation finished");

        auto resolvedResult = resolvedDrvGoal->buildResult;

        // No `std::visit` for coroutines yet
        if (auto * successP = resolvedResult.tryGetSuccess()) {
            auto & success = *successP;
            auto outputHashes = staticOutputHashes(worker.evalStore, *drv);
            auto resolvedHashes = staticOutputHashes(worker.store, drvResolved);

            auto outputHash = get(outputHashes, wantedOutput);
            auto resolvedHash = get(resolvedHashes, wantedOutput);
            if ((!outputHash) || (!resolvedHash))
                throw Error(
                    "derivation '%s' doesn't have expected output '%s' (derivation-goal.cc/resolve)",
                    worker.store.printStorePath(drvPath),
                    wantedOutput);

            auto realisation = [&] {
                auto take1 = get(success.builtOutputs, wantedOutput);
                if (take1)
                    return static_cast<UnkeyedRealisation>(*take1);

                /* The above `get` should work. But stateful tracking of
                   outputs in resolvedResult, this can get out of sync with the
                   store, which is our actual source of truth. For now we just
                   check the store directly if it fails. */
                auto take2 = worker.evalStore.queryRealisation(
                    DrvOutput{
                        .drvHash = *resolvedHash,
                        .outputName = wantedOutput,
                    });
                if (take2)
                    return *take2;

                throw Error(
                    "derivation '%s' doesn't have expected output '%s' (derivation-goal.cc/realisation)",
                    worker.store.printStorePath(pathResolved),
                    wantedOutput);
            }();

            if (!drv->type().isImpure()) {
                Realisation newRealisation{
                    realisation,
                    {
                        .drvHash = *outputHash,
                        .outputName = wantedOutput,
                    }};
                newRealisation.signatures.clear();
                if (!drv->type().isFixed()) {
                    auto & drvStore = worker.evalStore.isValidPath(drvPath) ? worker.evalStore : worker.store;
                    newRealisation.dependentRealisations =
                        drvOutputReferences(worker.store, *drv, realisation.outPath, &drvStore);
                }
                worker.store.signRealisation(newRealisation);
                worker.store.registerDrvOutput(newRealisation);
            }

            auto status = success.status;
            if (status == BuildResult::Success::AlreadyValid)
                status = BuildResult::Success::ResolvesToAlreadyValid;

            co_return doneSuccess(status, std::move(realisation));
        } else if (resolvedResult.tryGetFailure()) {
            co_return doneFailure({
                BuildResult::Failure::DependencyFailed,
                "build of resolved derivation '%s' failed",
                worker.store.printStorePath(pathResolved),
            });
        } else
            assert(false);
    }

    /* Give up on substitution for the output we want, actually build this derivation */

    auto g = worker.makeDerivationBuildingGoal(drvPath, *drv, buildMode, storeDerivation);

    /* We will finish with it ourselves, as if we were the derivational goal. */
    g->preserveException = true;

    {
        Goals waitees;
        waitees.insert(g);
        co_await await(std::move(waitees));
    }

    trace("outer build done");

    buildResult = g->buildResult;

    if (auto * successP = buildResult.tryGetSuccess()) {
        auto & success = *successP;
        if (buildMode == bmCheck) {
            /* In checking mode, the builder will not register any outputs.
               So we want to make sure the ones that we wanted to check are
               properly there. */
            success.builtOutputs = {{
                wantedOutput,
                {
                    assertPathValidity(),
                    {
                        .drvHash = outputHash,
                        .outputName = wantedOutput,
                    },
                },
            }};
        } else {
            /* Otherwise the builder will give us info for out output, but
               also for other outputs. Filter down to just our output so as
               not to leak info on unrelated things. */
            for (auto it = success.builtOutputs.begin(); it != success.builtOutputs.end();) {
                if (it->first != wantedOutput) {
                    it = success.builtOutputs.erase(it);
                } else {
                    ++it;
                }
            }

            /* If the wanted output is not in builtOutputs (e.g., because it
               was already valid and therefore not re-registered), we need to
               add it ourselves to ensure we return the correct information. */
            if (success.builtOutputs.count(wantedOutput) == 0) {
                debug(
                    "BUG! wanted output '%s' not in builtOutputs, working around by adding it manually", wantedOutput);
                success.builtOutputs = {{
                    wantedOutput,
                    {
                        assertPathValidity(),
                        {
                            .drvHash = outputHash,
                            .outputName = wantedOutput,
                        },
                    },
                }};
            }
        }
    }

    co_return amDone(g->exitCode, g->ex);
}

Goal::Co DerivationGoal::repairClosure()
{
    assert(!drv->type().isImpure());

    /* If we're repairing, we now know that our own outputs are valid.
       Now check whether the other paths in the outputs closure are
       good.  If not, then start derivation goals for the derivations
       that produced those outputs. */

    /* Get the output closure. */
    auto outputs = [&] {
        for (auto * drvStore : {&worker.evalStore, &worker.store})
            if (drvStore->isValidPath(drvPath))
                return worker.store.queryDerivationOutputMap(drvPath, drvStore);

        OutputPathMap res;
        for (auto & [name, output] : drv->outputsAndOptPaths(worker.store))
            res.insert_or_assign(name, *output.second);
        return res;
    }();

    StorePathSet outputClosure;
    if (auto * mPath = get(outputs, wantedOutput)) {
        worker.store.computeFSClosure(*mPath, outputClosure);
    }

    /* Filter out our own outputs (which we have already checked). */
    for (auto & i : outputs)
        outputClosure.erase(i.second);

    /* Get all dependencies of this derivation so that we know which
       derivation is responsible for which path in the output
       closure. */
    StorePathSet inputClosure;

    /* If we're working from an in-memory derivation with no in-store
       `*.drv` file, we cannot do this part. */
    if (worker.store.isValidPath(drvPath))
        worker.store.computeFSClosure(drvPath, inputClosure);

    std::map<StorePath, StorePath> outputsToDrv;
    for (auto & i : inputClosure)
        if (i.isDerivation()) {
            auto depOutputs = worker.store.queryPartialDerivationOutputMap(i, &worker.evalStore);
            for (auto & j : depOutputs)
                if (j.second)
                    outputsToDrv.insert_or_assign(*j.second, i);
        }

    Goals waitees;

    /* Check each path (slow!). */
    for (auto & i : outputClosure) {
        if (worker.pathContentsGood(i))
            continue;
        printError(
            "found corrupted or missing path '%s' in the output closure of '%s'",
            worker.store.printStorePath(i),
            worker.store.printStorePath(drvPath));
        auto drvPath2 = outputsToDrv.find(i);
        if (drvPath2 == outputsToDrv.end())
            waitees.insert(upcast_goal(worker.makePathSubstitutionGoal(i, Repair)));
        else
            waitees.insert(worker.makeGoal(
                DerivedPath::Built{
                    .drvPath = makeConstantStorePathRef(drvPath2->second),
                    .outputs = OutputsSpec::All{},
                },
                bmRepair));
    }

    bool haveWaitees = !waitees.empty();
    co_await await(std::move(waitees));

    if (haveWaitees) {
        trace("closure repaired");
        if (nrFailed > 0)
            throw Error(
                "some paths in the output closure of derivation '%s' could not be repaired",
                worker.store.printStorePath(drvPath));
    }
    co_return doneSuccess(BuildResult::Success::AlreadyValid, assertPathValidity());
}

std::optional<std::pair<UnkeyedRealisation, PathStatus>> DerivationGoal::checkPathValidity()
{
    if (drv->type().isImpure())
        return std::nullopt;

    auto drvOutput = DrvOutput{outputHash, wantedOutput};

    std::optional<UnkeyedRealisation> mRealisation;

    if (auto * mOutput = get(drv->outputs, wantedOutput)) {
        if (auto mPath = mOutput->path(worker.store, drv->name, wantedOutput)) {
            mRealisation = UnkeyedRealisation{
                .outPath = std::move(*mPath),
            };
        }
    } else {
        throw Error(
            "derivation '%s' does not have wanted outputs '%s'", worker.store.printStorePath(drvPath), wantedOutput);
    }

    if (experimentalFeatureSettings.isEnabled(Xp::CaDerivations)) {
        for (auto * drvStore : {&worker.evalStore, &worker.store}) {
            if (auto real = drvStore->queryRealisation(drvOutput)) {
                mRealisation = *real;
                break;
            }
        }
    }

    if (mRealisation) {
        auto & outputPath = mRealisation->outPath;
        bool checkHash = buildMode == bmRepair;
        PathStatus status = !worker.store.isValidPath(outputPath)               ? PathStatus::Absent
                            : !checkHash || worker.pathContentsGood(outputPath) ? PathStatus::Valid
                                                                                : PathStatus::Corrupt;

        if (experimentalFeatureSettings.isEnabled(Xp::CaDerivations) && status == PathStatus::Valid) {
            // We know the output because it's a static output of the
            // derivation, and the output path is valid, but we don't have
            // its realisation stored (probably because it has been built
            // without the `ca-derivations` experimental flag).
            worker.store.registerDrvOutput(
                Realisation{
                    *mRealisation,
                    {
                        .drvHash = outputHash,
                        .outputName = wantedOutput,
                    },
                });
        }

        return {{*mRealisation, status}};
    } else
        return std::nullopt;
}

UnkeyedRealisation DerivationGoal::assertPathValidity()
{
    auto checkResult = checkPathValidity();
    if (!(checkResult && checkResult->second == PathStatus::Valid))
        throw Error("some outputs are unexpectedly invalid");
    return checkResult->first;
}

Goal::Done DerivationGoal::doneSuccess(BuildResult::Success::Status status, UnkeyedRealisation builtOutput)
{
    buildResult.inner = BuildResult::Success{
        .status = status,
        .builtOutputs = {{
            wantedOutput,
            {
                std::move(builtOutput),
                DrvOutput{
                    .drvHash = outputHash,
                    .outputName = wantedOutput,
                },
            },
        }},
    };

    mcExpectedBuilds.reset();

    if (status == BuildResult::Success::Built)
        worker.doneBuilds++;

    worker.updateProgress();

    return amDone(ecSuccess, std::nullopt);
}

Goal::Done DerivationGoal::doneFailure(BuildError ex)
{
    buildResult.inner = BuildResult::Failure{
        .status = ex.status,
        .errorMsg = fmt("%s", Uncolored(ex.info().msg)),
    };

    mcExpectedBuilds.reset();

    if (ex.status == BuildResult::Failure::TimedOut)
        worker.timedOut = true;
    if (ex.status == BuildResult::Failure::PermanentFailure)
        worker.permanentFailure = true;
    if (ex.status != BuildResult::Failure::DependencyFailed)
        worker.failedBuilds++;

    worker.updateProgress();

    return amDone(ecFailed, {std::move(ex)});
}

} // namespace nix
