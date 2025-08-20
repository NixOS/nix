#include "nix/store/build/derivation-goal.hh"
#include "nix/store/build/derivation-building-goal.hh"
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
    BuildMode buildMode)
    : Goal(worker, haveDerivation())
    , drvPath(drvPath)
    , wantedOutput(wantedOutput)
    , outputHash{[&] {
        auto outputHashes = staticOutputHashes(worker.evalStore, drv);
        if (auto * mOutputHash = get(outputHashes, wantedOutput))
            return *mOutputHash;
        throw Error("derivation '%s' does not have output '%s'", worker.store.printStorePath(drvPath), wantedOutput);
    }()}
    , buildMode(buildMode)
{
    this->drv = std::make_unique<Derivation>(drv);

    name =
        fmt("building of '%s' from in-memory derivation",
            DerivedPath::Built{makeConstantStorePathRef(drvPath), drv.outputNames()}.to_string(worker.store));
    trace("created");

    mcExpectedBuilds = std::make_unique<MaintainCount<uint64_t>>(worker.expectedBuilds);
    worker.updateProgress();
}

std::string DerivationGoal::key()
{
    /* Ensure that derivations get built in order of their name,
       i.e. a derivation named "aardvark" always comes before
       "baboon". And substitution goals always happen before
       derivation goals (due to "b$"). */
    return "b$" + std::string(drvPath.name()) + "$" + SingleDerivedPath::Built{
        .drvPath = makeConstantStorePathRef(drvPath),
        .output = wantedOutput,
    }.to_string(worker.store);
}

Goal::Co DerivationGoal::haveDerivation()
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
            co_return done(BuildResult::AlreadyValid, checkResult->first);
        }

        Goals waitees;

        /* We are first going to try to create the invalid output paths
           through substitutes.  If that doesn't work, we'll build
           them. */
        if (settings.useSubstitutes && drvOptions.substitutesAllowed()) {
            if (!checkResult)
                waitees.insert(upcast_goal(worker.makeDrvOutputSubstitutionGoal(
                    DrvOutput{outputHash, wantedOutput}, buildMode == bmRepair ? Repair : NoRepair)));
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
            co_return done(
                BuildResult::TransientFailure,
                {},
                Error(
                    "some substitutes for the outputs of derivation '%s' failed (usually happens due to networking issues); try '--fallback' to build derivation from source ",
                    worker.store.printStorePath(drvPath)));
        }

        nrFailed = nrNoSubstituters = 0;

        checkResult = checkPathValidity();

        bool allValid = checkResult && checkResult->second == PathStatus::Valid;

        if (buildMode == bmNormal && allValid) {
            co_return done(BuildResult::Substituted, checkResult->first);
        }
        if (buildMode == bmRepair && allValid) {
            co_return repairClosure();
        }
        if (buildMode == bmCheck && !allValid)
            throw Error(
                "some outputs of '%s' are not valid, so checking is not possible",
                worker.store.printStorePath(drvPath));
    }

    /* Give up on substitution for the output we want, actually build this derivation */

    auto g = worker.makeDerivationBuildingGoal(drvPath, *drv, buildMode);

    /* We will finish with it ourselves, as if we were the derivational goal. */
    g->preserveException = true;

    {
        Goals waitees;
        waitees.insert(g);
        co_await await(std::move(waitees));
    }

    trace("outer build done");

    buildResult = g->buildResult;

    if (buildMode == bmCheck) {
        /* In checking mode, the builder will not register any outputs.
           So we want to make sure the ones that we wanted to check are
           properly there. */
        buildResult.builtOutputs = {{wantedOutput, assertPathValidity()}};
    } else {
        /* Otherwise the builder will give us info for out output, but
           also for other outputs. Filter down to just our output so as
           not to leak info on unrelated things. */
        for (auto it = buildResult.builtOutputs.begin(); it != buildResult.builtOutputs.end();) {
            if (it->first != wantedOutput) {
                it = buildResult.builtOutputs.erase(it);
            } else {
                ++it;
            }
        }

        if (buildResult.success())
            assert(buildResult.builtOutputs.count(wantedOutput) > 0);
    }

    co_return amDone(g->exitCode, g->ex);
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

    co_await await(std::move(waitees));

    if (!waitees.empty()) {
        trace("closure repaired");
        if (nrFailed > 0)
            throw Error(
                "some paths in the output closure of derivation '%s' could not be repaired",
                worker.store.printStorePath(drvPath));
    }
    co_return done(BuildResult::AlreadyValid, assertPathValidity());
}

std::optional<std::pair<Realisation, PathStatus>> DerivationGoal::checkPathValidity()
{
    if (drv->type().isImpure())
        return std::nullopt;

    auto drvOutput = DrvOutput{outputHash, wantedOutput};

    std::optional<Realisation> mRealisation;

    if (auto * mOutput = get(drv->outputs, wantedOutput)) {
        if (auto mPath = mOutput->path(worker.store, drv->name, wantedOutput)) {
            mRealisation = Realisation{drvOutput, std::move(*mPath)};
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
            worker.store.registerDrvOutput(*mRealisation);
        }

        return {{*mRealisation, status}};
    } else
        return std::nullopt;
}

Realisation DerivationGoal::assertPathValidity()
{
    auto checkResult = checkPathValidity();
    if (!(checkResult && checkResult->second == PathStatus::Valid))
        throw Error("some outputs are unexpectedly invalid");
    return checkResult->first;
}

Goal::Done
DerivationGoal::done(BuildResult::Status status, std::optional<Realisation> builtOutput, std::optional<Error> ex)
{
    buildResult.status = status;
    if (ex)
        buildResult.errorMsg = fmt("%s", Uncolored(ex->info().msg));
    if (buildResult.status == BuildResult::TimedOut)
        worker.timedOut = true;
    if (buildResult.status == BuildResult::PermanentFailure)
        worker.permanentFailure = true;

    mcExpectedBuilds.reset();

    if (buildResult.success()) {
        assert(builtOutput);
        buildResult.builtOutputs = {{wantedOutput, std::move(*builtOutput)}};
        if (status == BuildResult::Built)
            worker.doneBuilds++;
    } else {
        if (status != BuildResult::DependencyFailed)
            worker.failedBuilds++;
    }

    worker.updateProgress();

    auto traceBuiltOutputsFile = getEnv("_NIX_TRACE_BUILT_OUTPUTS").value_or("");
    if (traceBuiltOutputsFile != "") {
        std::fstream fs;
        fs.open(traceBuiltOutputsFile, std::fstream::out);
        fs << worker.store.printStorePath(drvPath) << "\t" << buildResult.toString() << std::endl;
    }

    return amDone(buildResult.success() ? ecSuccess : ecFailed, std::move(ex));
}

} // namespace nix
