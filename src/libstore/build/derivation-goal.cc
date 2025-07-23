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
#include "nix/store/local-store.hh"          // TODO remove, along with remaining downcasts

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
        auto parsedOpt = StructuredAttrs::tryParse(drv->env);
        try {
            return DerivationOptions::fromStructuredAttrs(drv->env, parsedOpt ? &*parsedOpt : nullptr);
        } catch (Error & e) {
            e.addTrace({}, "while parsing derivation '%s'", worker.store.printStorePath(drvPath));
            throw;
        }
    }();

    if (!drv->type().hasKnownOutputPaths())
        experimentalFeatureSettings.require(Xp::CaDerivations);

    /* At least one of the output paths could not be
       produced using a substitute.  So we have to build instead. */
    auto gaveUpOnSubstitution = [&]() -> Goal::Co {
        auto g = worker.makeDerivationBuildingGoal(drvPath, *drv, buildMode);

        /* We will finish with it ourselves, as if we were the derivational goal. */
        g->preserveException = true;

        // TODO move into constructor
        g->initialOutputs = initialOutputs;

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
            buildResult.builtOutputs = assertPathValidity();
        }

        for (auto it = buildResult.builtOutputs.begin(); it != buildResult.builtOutputs.end();) {
            if (it->first != wantedOutput) {
                it = buildResult.builtOutputs.erase(it);
            } else {
                ++it;
            }
        }

        if (buildResult.success())
            assert(buildResult.builtOutputs.count(wantedOutput) > 0);

        co_return amDone(g->exitCode, g->ex);
    };

    for (auto & i : drv->outputsAndOptPaths(worker.store))
        if (i.second.second)
            worker.store.addTempRoot(*i.second.second);

    {
        bool impure = drv->type().isImpure();

        if (impure)
            experimentalFeatureSettings.require(Xp::ImpureDerivations);

        auto outputHashes = staticOutputHashes(worker.evalStore, *drv);
        for (auto & [outputName, outputHash] : outputHashes) {
            InitialOutput v{
                .wanted = true, // Will be refined later
                .outputHash = outputHash};

            /* TODO we might want to also allow randomizing the paths
               for regular CA derivations, e.g. for sake of checking
               determinism. */
            if (impure) {
                v.known = InitialOutputStatus{
                    .path = StorePath::random(outputPathName(drv->name, outputName)),
                    .status = PathStatus::Absent,
                };
            }

            initialOutputs.insert({
                outputName,
                std::move(v),
            });
        }

        if (impure) {
            /* We don't yet have any safe way to cache an impure derivation at
               this step. */
            co_return gaveUpOnSubstitution();
        }
    }

    {
        /* Check what outputs paths are not already valid. */
        auto [allValid, validOutputs] = checkPathValidity();

        /* If they are all valid, then we're done. */
        if (allValid && buildMode == bmNormal) {
            co_return done(BuildResult::AlreadyValid, std::move(validOutputs));
        }
    }

    Goals waitees;

    /* We are first going to try to create the invalid output paths
       through substitutes.  If that doesn't work, we'll build
       them. */
    if (settings.useSubstitutes && drvOptions.substitutesAllowed())
        for (auto & [outputName, status] : initialOutputs) {
            if (!status.wanted)
                continue;
            if (!status.known)
                waitees.insert(upcast_goal(worker.makeDrvOutputSubstitutionGoal(
                    DrvOutput{status.outputHash, outputName}, buildMode == bmRepair ? Repair : NoRepair)));
            else {
                auto * cap = getDerivationCA(*drv);
                waitees.insert(upcast_goal(worker.makePathSubstitutionGoal(
                    status.known->path,
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

    auto [allValid, validOutputs] = checkPathValidity();

    if (buildMode == bmNormal && allValid) {
        co_return done(BuildResult::Substituted, std::move(validOutputs));
    }
    if (buildMode == bmRepair && allValid) {
        co_return repairClosure();
    }
    if (buildMode == bmCheck && !allValid)
        throw Error(
            "some outputs of '%s' are not valid, so checking is not possible", worker.store.printStorePath(drvPath));

    /* Nothing to wait for; tail call */
    co_return gaveUpOnSubstitution();
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
    auto outputs = queryDerivationOutputMap();
    StorePathSet outputClosure;
    if (auto mPath = get(outputs, wantedOutput)) {
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

std::map<std::string, std::optional<StorePath>> DerivationGoal::queryPartialDerivationOutputMap()
{
    assert(!drv->type().isImpure());

    for (auto * drvStore : {&worker.evalStore, &worker.store})
        if (drvStore->isValidPath(drvPath))
            return worker.store.queryPartialDerivationOutputMap(drvPath, drvStore);

    /* In-memory derivation will naturally fall back on this case, where
       we do best-effort with static information. */
    std::map<std::string, std::optional<StorePath>> res;
    for (auto & [name, output] : drv->outputs)
        res.insert_or_assign(name, output.path(worker.store, drv->name, name));
    return res;
}

OutputPathMap DerivationGoal::queryDerivationOutputMap()
{
    assert(!drv->type().isImpure());

    for (auto * drvStore : {&worker.evalStore, &worker.store})
        if (drvStore->isValidPath(drvPath))
            return worker.store.queryDerivationOutputMap(drvPath, drvStore);

    // See comment in `DerivationGoal::queryPartialDerivationOutputMap`.
    OutputPathMap res;
    for (auto & [name, output] : drv->outputsAndOptPaths(worker.store))
        res.insert_or_assign(name, *output.second);
    return res;
}

std::pair<bool, SingleDrvOutputs> DerivationGoal::checkPathValidity()
{
    if (drv->type().isImpure())
        return {false, {}};

    bool checkHash = buildMode == bmRepair;
    StringSet wantedOutputsLeft{wantedOutput};
    SingleDrvOutputs validOutputs;

    for (auto & i : queryPartialDerivationOutputMap()) {
        auto initialOutput = get(initialOutputs, i.first);
        if (!initialOutput)
            // this is an invalid output, gets caught with (!wantedOutputsLeft.empty())
            continue;
        auto & info = *initialOutput;
        info.wanted = wantedOutput == i.first;
        if (info.wanted)
            wantedOutputsLeft.erase(i.first);
        if (i.second) {
            auto outputPath = *i.second;
            info.known = {
                .path = outputPath,
                .status = !worker.store.isValidPath(outputPath)               ? PathStatus::Absent
                          : !checkHash || worker.pathContentsGood(outputPath) ? PathStatus::Valid
                                                                              : PathStatus::Corrupt,
            };
        }
        auto drvOutput = DrvOutput{info.outputHash, i.first};
        if (experimentalFeatureSettings.isEnabled(Xp::CaDerivations)) {
            if (auto real = worker.store.queryRealisation(drvOutput)) {
                info.known = {
                    .path = real->outPath,
                    .status = PathStatus::Valid,
                };
            } else if (info.known && info.known->isValid()) {
                // We know the output because it's a static output of the
                // derivation, and the output path is valid, but we don't have
                // its realisation stored (probably because it has been built
                // without the `ca-derivations` experimental flag).
                worker.store.registerDrvOutput(
                    Realisation{
                        drvOutput,
                        info.known->path,
                    });
            }
        }
        if (info.known && info.known->isValid())
            validOutputs.emplace(i.first, Realisation{drvOutput, info.known->path});
    }

    // If we requested all the outputs, we are always fine.
    // If we requested specific elements, the loop above removes all the valid
    // ones, so any that are left must be invalid.
    if (!wantedOutputsLeft.empty())
        throw Error(
            "derivation '%s' does not have wanted outputs %s",
            worker.store.printStorePath(drvPath),
            concatStringsSep(", ", quoteStrings(wantedOutputsLeft)));

    bool allValid = true;
    for (auto & [_, status] : initialOutputs) {
        if (!status.wanted)
            continue;
        if (!status.known || !status.known->isValid()) {
            allValid = false;
            break;
        }
    }

    return {allValid, validOutputs};
}

SingleDrvOutputs DerivationGoal::assertPathValidity()
{
    auto [allValid, validOutputs] = checkPathValidity();
    if (!allValid)
        throw Error("some outputs are unexpectedly invalid");
    return validOutputs;
}

Goal::Done DerivationGoal::done(BuildResult::Status status, SingleDrvOutputs builtOutputs, std::optional<Error> ex)
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
        auto wantedBuiltOutputs = filterDrvOutputs(OutputsSpec::Names{wantedOutput}, std::move(builtOutputs));
        assert(!wantedBuiltOutputs.empty());
        buildResult.builtOutputs = std::move(wantedBuiltOutputs);
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
