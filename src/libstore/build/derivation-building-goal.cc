#include "nix/store/build/derivation-building-goal.hh"
#include "nix/store/build/derivation-env-desugar.hh"
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
#include "nix/store/common-protocol-impl.hh"
#include "nix/store/local-store.hh" // TODO remove, along with remaining downcasts
#include "nix/store/globals.hh"

#include <fstream>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>

#include <nlohmann/json.hpp>

#include "nix/util/strings.hh"

namespace nix {

DerivationBuildingGoal::DerivationBuildingGoal(
    const StorePath & drvPath, const Derivation & drv, Worker & worker, BuildMode buildMode, bool storeDerivation)
    : Goal(worker, gaveUpOnSubstitution(storeDerivation))
    , drvPath(drvPath)
    , drv{std::make_unique<Derivation>(drv)}
    , buildMode(buildMode)
{
    try {
        drvOptions =
            std::make_unique<DerivationOptions>(DerivationOptions::fromStructuredAttrs(drv.env, drv.structuredAttrs));
    } catch (Error & e) {
        e.addTrace({}, "while parsing derivation '%s'", worker.store.printStorePath(drvPath));
        throw;
    }

    name = fmt("building derivation '%s'", worker.store.printStorePath(drvPath));
    trace("created");

    /* Prevent the .chroot directory from being
       garbage-collected. (See isActiveTempFile() in gc.cc.) */
    worker.store.addTempRoot(this->drvPath);
}

DerivationBuildingGoal::~DerivationBuildingGoal()
{
    /* Careful: we should never ever throw an exception from a
       destructor. */
#ifndef _WIN32 // TODO enable `DerivationBuilder` on Windows
    if (builder)
        builder.reset();
#endif
    try {
        closeLogFile();
    } catch (...) {
        ignoreExceptionInDestructor();
    }
}

std::string DerivationBuildingGoal::key()
{
    return "dd$" + std::string(drvPath.name()) + "$" + worker.store.printStorePath(drvPath);
}

void DerivationBuildingGoal::killChild()
{
#ifndef _WIN32 // TODO enable build hook on Windows
    hook.reset();
#endif
#ifndef _WIN32 // TODO enable `DerivationBuilder` on Windows
    if (builder && builder->killChild())
        worker.childTerminated(this);
#endif
}

void DerivationBuildingGoal::timedOut(Error && ex)
{
    killChild();
    // We're not inside a coroutine, hence we can't use co_return here.
    // Thus we ignore the return value.
    [[maybe_unused]] Done _ = doneFailure({BuildResult::Failure::TimedOut, std::move(ex)});
}

std::string showKnownOutputs(const StoreDirConfig & store, const Derivation & drv)
{
    std::string msg;
    StorePathSet expectedOutputPaths;
    for (auto & i : drv.outputsAndOptPaths(store))
        if (i.second.second)
            expectedOutputPaths.insert(*i.second.second);
    if (!expectedOutputPaths.empty()) {
        msg += "\nOutput paths:";
        for (auto & p : expectedOutputPaths)
            msg += fmt("\n  %s", Magenta(store.printStorePath(p)));
    }
    return msg;
}

static void runPostBuildHook(
    const StoreDirConfig & store, Logger & logger, const StorePath & drvPath, const StorePathSet & outputPaths);

/* At least one of the output paths could not be
   produced using a substitute.  So we have to build instead. */
Goal::Co DerivationBuildingGoal::gaveUpOnSubstitution(bool storeDerivation)
{
    Goals waitees;

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
        co_return doneFailure(BuildError(BuildResult::Failure::DependencyFailed, msg));
    }

    /* Gather information necessary for computing the closure and/or
       running the build hook. */

    /* Determine the full set of input paths. */

    if (storeDerivation) {
        assert(drv->inputDrvs.map.empty());
        /* Store the resolved derivation, as part of the record of
           what we're actually building */
        writeDerivation(worker.store, *drv);
    }

    {
        /* If we get this far, we know no dynamic drvs inputs */

        for (auto & [depDrvPath, depNode] : drv->inputDrvs.map) {
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
    co_return tryToBuild();
}

Goal::Co DerivationBuildingGoal::tryToBuild()
{
    std::map<std::string, InitialOutput> initialOutputs;

    /* Recheck at this point. In particular, whereas before we were
       given this information by the downstream goal, that cannot happen
       anymore if the downstream goal only cares about one output, but
       we care about all outputs. */
    auto outputHashes = staticOutputHashes(worker.evalStore, *drv);
    for (auto & [outputName, outputHash] : outputHashes) {
        InitialOutput v{.outputHash = outputHash};

        /* TODO we might want to also allow randomizing the paths
           for regular CA derivations, e.g. for sake of checking
           determinism. */
        if (drv->type().isImpure()) {
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
    checkPathValidity(initialOutputs);

    auto started = [&]() {
        auto msg =
            fmt(buildMode == bmRepair  ? "repairing outputs of '%s'"
                : buildMode == bmCheck ? "checking outputs of '%s'"
                                       : "building '%s'",
                worker.store.printStorePath(drvPath));
#ifndef _WIN32 // TODO enable build hook on Windows
        if (hook)
            msg += fmt(" on '%s'", hook->machineName);
#endif
        act = std::make_unique<Activity>(
            *logger,
            lvlInfo,
            actBuild,
            msg,
            Logger::Fields{
                worker.store.printStorePath(drvPath),
#ifndef _WIN32 // TODO enable build hook on Windows
                hook ? hook->machineName :
#endif
                     "",
                1,
                1});
        mcRunningBuilds = std::make_unique<MaintainCount<uint64_t>>(worker.runningBuilds);
        worker.updateProgress();
    };

    /**
     * Activity that denotes waiting for a lock.
     */
    std::unique_ptr<Activity> actLock;

    /**
     * Locks on (fixed) output paths.
     */
    PathLocks outputLocks;

    bool useHook;

    const ExternalBuilder * externalBuilder = nullptr;

    while (true) {
        trace("trying to build");

        /* Obtain locks on all output paths, if the paths are known a priori.

           The locks are automatically released when we exit this function or Nix
           crashes.  If we can't acquire the lock, then continue; hopefully some
           other goal can start a build, and if not, the main loop will sleep a few
           seconds and then retry this goal. */
        PathSet lockFiles;
        /* FIXME: Should lock something like the drv itself so we don't build same
           CA drv concurrently */
        if (auto * localStore = dynamic_cast<LocalStore *>(&worker.store)) {
            /* If we aren't a local store, we might need to use the local store as
               a build remote, but that would cause a deadlock. */
            /* FIXME: Make it so we can use ourselves as a build remote even if we
               are the local store (separate locking for building vs scheduling? */
            /* FIXME: find some way to lock for scheduling for the other stores so
               a forking daemon with --store still won't farm out redundant builds.
               */
            for (auto & i : drv->outputsAndOptPaths(worker.store)) {
                if (i.second.second)
                    lockFiles.insert(localStore->toRealPath(*i.second.second));
                else
                    lockFiles.insert(localStore->toRealPath(drvPath) + "." + i.first);
            }
        }

        if (!outputLocks.lockPaths(lockFiles, "", false)) {
            Activity act(
                *logger, lvlWarn, actBuildWaiting, fmt("waiting for lock on %s", Magenta(showPaths(lockFiles))));

            /* Wait then try locking again, repeat until success (returned
               boolean is true). */
            do {
                co_await waitForAWhile();
            } while (!outputLocks.lockPaths(lockFiles, "", false));
        }

        /* Now check again whether the outputs are valid.  This is because
           another process may have started building in parallel.  After
           it has finished and released the locks, we can (and should)
           reuse its results.  (Strictly speaking the first check can be
           omitted, but that would be less efficient.)  Note that since we
           now hold the locks on the output paths, no other process can
           build this derivation, so no further checks are necessary. */
        auto [allValid, validOutputs] = checkPathValidity(initialOutputs);

        if (buildMode != bmCheck && allValid) {
            debug("skipping build of derivation '%s', someone beat us to it", worker.store.printStorePath(drvPath));
            outputLocks.setDeletion(true);
            outputLocks.unlock();
            co_return doneSuccess(BuildResult::Success::AlreadyValid, std::move(validOutputs));
        }

        /* If any of the outputs already exist but are not valid, delete
           them. */
        if (auto * localStore = dynamic_cast<LocalFSStore *>(&worker.store)) {
            for (auto & [_, status] : initialOutputs) {
                if (!status.known || status.known->isValid())
                    continue;
                auto storePath = status.known->path;
                debug("removing invalid path '%s'", worker.store.printStorePath(status.known->path));
                deletePath(localStore->toRealPath(storePath));
            }
        }

        /* Don't do a remote build if the derivation has the attribute
           `preferLocalBuild' set.  Also, check and repair modes are only
           supported for local builds. */
        bool buildLocally = (buildMode != bmNormal || drvOptions->willBuildLocally(worker.store, *drv))
                            && settings.maxBuildJobs.get() != 0;

        if (buildLocally) {
            useHook = false;
        } else {
            switch (tryBuildHook(initialOutputs)) {
            case rpAccept:
                /* Yes, it has started doing so.  Wait until we get
                   EOF from the hook. */
                useHook = true;
                break;
            case rpPostpone:
                /* Not now; wait until at least one child finishes or
                   the wake-up timeout expires. */
                if (!actLock)
                    actLock = std::make_unique<Activity>(
                        *logger,
                        lvlWarn,
                        actBuildWaiting,
                        fmt("waiting for a machine to build '%s'", Magenta(worker.store.printStorePath(drvPath))));
                outputLocks.unlock();
                co_await waitForAWhile();
                continue;
            case rpDecline:
                /* We should do it ourselves.

                   Now that we've decided we can't / won't do a remote build, check
                   that we can in fact build locally. First see if there is an
                   external builder for a "semi-local build". If there is, prefer to
                   use that. If there is not, then check if we can do a "true" local
                   build. */

                externalBuilder = settings.findExternalDerivationBuilderIfSupported(*drv);

                if (!externalBuilder && !drvOptions->canBuildLocally(worker.store, *drv)) {
                    auto msg =
                        fmt("Cannot build '%s'.\n"
                            "Reason: " ANSI_RED "required system or feature not available" ANSI_NORMAL
                            "\n"
                            "Required system: '%s' with features {%s}\n"
                            "Current system: '%s' with features {%s}",
                            Magenta(worker.store.printStorePath(drvPath)),
                            Magenta(drv->platform),
                            concatStringsSep(", ", drvOptions->getRequiredSystemFeatures(*drv)),
                            Magenta(settings.thisSystem),
                            concatStringsSep<StringSet>(", ", worker.store.Store::config.systemFeatures));

                    // since aarch64-darwin has Rosetta 2, this user can actually run x86_64-darwin on their hardware -
                    // we should tell them to run the command to install Darwin 2
                    if (drv->platform == "x86_64-darwin" && settings.thisSystem == "aarch64-darwin")
                        msg += fmt(
                            "\nNote: run `%s` to run programs for x86_64-darwin",
                            Magenta(
                                "/usr/sbin/softwareupdate --install-rosetta && launchctl stop org.nixos.nix-daemon"));

#ifndef _WIN32 // TODO enable `DerivationBuilder` on Windows
                    builder.reset();
#endif
                    outputLocks.unlock();
                    worker.permanentFailure = true;
                    co_return doneFailure({BuildResult::Failure::InputRejected, std::move(msg)});
                }
                useHook = false;
                break;
            }
        }
        break;
    }

    actLock.reset();

    if (useHook) {
        buildResult.startTime = time(0); // inexact
        started();
        co_await Suspend{};

#ifndef _WIN32
        assert(hook);
#endif

        trace("hook build done");

        /* Since we got an EOF on the logger pipe, the builder is presumed
           to have terminated.  In fact, the builder could also have
           simply have closed its end of the pipe, so just to be sure,
           kill it. */
        int status =
#ifndef _WIN32 // TODO enable build hook on Windows
            hook->pid.kill();
#else
            0;
#endif

        debug("build hook for '%s' finished", worker.store.printStorePath(drvPath));

        buildResult.timesBuilt++;
        buildResult.stopTime = time(0);

        /* So the child is gone now. */
        worker.childTerminated(this);

        /* Close the read side of the logger pipe. */
#ifndef _WIN32 // TODO enable build hook on Windows
        hook->builderOut.readSide.close();
        hook->fromHook.readSide.close();
#endif

        /* Close the log file. */
        closeLogFile();

        /* Check the exit status. */
        if (!statusOk(status)) {
            auto e = fixupBuilderFailureErrorMessage({BuildResult::Failure::MiscFailure, status, ""});

            outputLocks.unlock();

            /* TODO (once again) support fine-grained error codes, see issue #12641. */

            co_return doneFailure(std::move(e));
        }

        /* Compute the FS closure of the outputs and register them as
           being valid. */
        auto builtOutputs =
            /* When using a build hook, the build hook can register the output
               as valid (by doing `nix-store --import').  If so we don't have
               to do anything here.

               We can only early return when the outputs are known a priori. For
               floating content-addressing derivations this isn't the case.

               Aborts if any output is not valid or corrupt, and otherwise
               returns a 'SingleDrvOutputs' structure containing all outputs.
             */
            [&] {
                auto [allValid, validOutputs] = checkPathValidity(initialOutputs);
                if (!allValid)
                    throw Error("some outputs are unexpectedly invalid");
                return validOutputs;
            }();

        StorePathSet outputPaths;
        for (auto & [_, output] : builtOutputs)
            outputPaths.insert(output.outPath);
        runPostBuildHook(worker.store, *logger, drvPath, outputPaths);

        /* It is now safe to delete the lock files, since all future
           lockers will see that the output paths are valid; they will
           not create new lock files with the same names as the old
           (unlinked) lock files. */
        outputLocks.setDeletion(true);
        outputLocks.unlock();

        co_return doneSuccess(BuildResult::Success::Built, std::move(builtOutputs));
    }

    co_await yield();

    if (!dynamic_cast<LocalStore *>(&worker.store)) {
        throw Error(
            R"(
            Unable to build with a primary store that isn't a local store;
            either pass a different '--store' or enable remote builds.

            For more information check 'man nix.conf' and search for '/machines'.
            )");
    }

#ifdef _WIN32 // TODO enable `DerivationBuilder` on Windows
    throw UnimplementedError("building derivations is not yet implemented on Windows");
#else
    assert(!hook);

    Descriptor builderOut;

    // Will continue here while waiting for a build user below
    while (true) {

        unsigned int curBuilds = worker.getNrLocalBuilds();
        if (curBuilds >= settings.maxBuildJobs) {
            outputLocks.unlock();
            co_await waitForBuildSlot();
            co_return tryToBuild();
        }

        if (!builder) {
            /**
             * Local implementation of these virtual methods, consider
             * this just a record of lambdas.
             */
            struct DerivationBuildingGoalCallbacks : DerivationBuilderCallbacks
            {
                DerivationBuildingGoal & goal;

                DerivationBuildingGoalCallbacks(
                    DerivationBuildingGoal & goal, std::unique_ptr<DerivationBuilder> & builder)
                    : goal{goal}
                {
                }

                ~DerivationBuildingGoalCallbacks() override = default;

                void childTerminated() override
                {
                    goal.worker.childTerminated(&goal);
                }

                Path openLogFile() override
                {
                    return goal.openLogFile();
                }

                void closeLogFile() override
                {
                    goal.closeLogFile();
                }
            };

            auto * localStoreP = dynamic_cast<LocalStore *>(&worker.store);
            assert(localStoreP);

            decltype(DerivationBuilderParams::defaultPathsInChroot) defaultPathsInChroot = settings.sandboxPaths.get();
            DesugaredEnv desugaredEnv;

            /* Add the closure of store paths to the chroot. */
            StorePathSet closure;
            for (auto & i : defaultPathsInChroot)
                try {
                    if (worker.store.isInStore(i.second.source))
                        worker.store.computeFSClosure(worker.store.toStorePath(i.second.source).first, closure);
                } catch (InvalidPath & e) {
                } catch (Error & e) {
                    e.addTrace({}, "while processing sandbox path '%s'", i.second.source);
                    throw;
                }
            for (auto & i : closure) {
                auto p = worker.store.printStorePath(i);
                defaultPathsInChroot.insert_or_assign(p, ChrootPath{.source = p});
            }

            try {
                desugaredEnv = DesugaredEnv::create(worker.store, *drv, *drvOptions, inputPaths);
            } catch (BuildError & e) {
                outputLocks.unlock();
                worker.permanentFailure = true;
                co_return doneFailure(std::move(e));
            }

            DerivationBuilderParams params{
                .drvPath = drvPath,
                .buildResult = buildResult,
                .drv = *drv,
                .drvOptions = *drvOptions,
                .inputPaths = inputPaths,
                .initialOutputs = initialOutputs,
                .buildMode = buildMode,
                .defaultPathsInChroot = std::move(defaultPathsInChroot),
                .systemFeatures = worker.store.config.systemFeatures.get(),
                .desugaredEnv = std::move(desugaredEnv),
            };

            /* If we have to wait and retry (see below), then `builder` will
               already be created, so we don't need to create it again. */
            builder = externalBuilder ? makeExternalDerivationBuilder(
                                            *localStoreP,
                                            std::make_unique<DerivationBuildingGoalCallbacks>(*this, builder),
                                            std::move(params),
                                            *externalBuilder)
                                      : makeDerivationBuilder(
                                            *localStoreP,
                                            std::make_unique<DerivationBuildingGoalCallbacks>(*this, builder),
                                            std::move(params));
        }

        if (auto builderOutOpt = builder->startBuild()) {
            builderOut = *std::move(builderOutOpt);
        } else {
            if (!actLock)
                actLock = std::make_unique<Activity>(
                    *logger,
                    lvlWarn,
                    actBuildWaiting,
                    fmt("waiting for a free build user ID for '%s'", Magenta(worker.store.printStorePath(drvPath))));
            co_await waitForAWhile();
            continue;
        }

        break;
    }

    actLock.reset();

    worker.childStarted(shared_from_this(), {builderOut}, true, true);

    started();
    co_await Suspend{};

    trace("build done");

    SingleDrvOutputs builtOutputs;
    try {
        builtOutputs = builder->unprepareBuild();
    } catch (BuilderFailureError & e) {
        builder.reset();
        outputLocks.unlock();
        co_return doneFailure(fixupBuilderFailureErrorMessage(std::move(e)));
    } catch (BuildError & e) {
        builder.reset();
        outputLocks.unlock();
// Allow selecting a subset of enum values
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wswitch-enum"
        switch (e.status) {
        case BuildResult::Failure::HashMismatch:
            worker.hashMismatch = true;
            /* See header, the protocols don't know about `HashMismatch`
               yet, so change it to `OutputRejected`, which they expect
               for this case (hash mismatch is a type of output
               rejection). */
            e.status = BuildResult::Failure::OutputRejected;
            break;
        case BuildResult::Failure::NotDeterministic:
            worker.checkMismatch = true;
            break;
        default:
            /* Other statuses need no adjusting */
            break;
        }
#  pragma GCC diagnostic pop
        co_return doneFailure(std::move(e));
    }
    {
        builder.reset();
        StorePathSet outputPaths;
        /* In the check case we install no store objects, and so
           `builtOutputs` is empty. However, per issue #14287, there is
           an expectation that the post-build hook is still executed.
           (This is useful for e.g. logging successful deterministic rebuilds.)

           In order to make that work, in the check case just load the
           (preexisting) infos from scratch, rather than relying on what
           `DerivationBuilder` returned to us. */
        for (auto & [_, output] : buildMode == bmCheck ? checkPathValidity(initialOutputs).second : builtOutputs) {
            // for sake of `bmRepair`
            worker.markContentsGood(output.outPath);
            outputPaths.insert(output.outPath);
        }
        runPostBuildHook(worker.store, *logger, drvPath, outputPaths);

        /* It is now safe to delete the lock files, since all future
           lockers will see that the output paths are valid; they will
           not create new lock files with the same names as the old
           (unlinked) lock files. */
        outputLocks.setDeletion(true);
        outputLocks.unlock();
        co_return doneSuccess(BuildResult::Success::Built, std::move(builtOutputs));
    }
#endif
}

static void runPostBuildHook(
    const StoreDirConfig & store, Logger & logger, const StorePath & drvPath, const StorePathSet & outputPaths)
{
    auto hook = settings.postBuildHook;
    if (hook == "")
        return;

    Activity act(
        logger,
        lvlTalkative,
        actPostBuildHook,
        fmt("running post-build-hook '%s'", settings.postBuildHook),
        Logger::Fields{store.printStorePath(drvPath)});
    PushActivity pact(act.id);
    StringMap hookEnvironment = getEnv();

    hookEnvironment.emplace("DRV_PATH", store.printStorePath(drvPath));
    hookEnvironment.emplace("OUT_PATHS", chomp(concatStringsSep(" ", store.printStorePathSet(outputPaths))));
    hookEnvironment.emplace("NIX_CONFIG", globalConfig.toKeyValue());

    struct LogSink : Sink
    {
        Activity & act;
        std::string currentLine;

        LogSink(Activity & act)
            : act(act)
        {
        }

        void operator()(std::string_view data) override
        {
            for (auto c : data) {
                if (c == '\n') {
                    flushLine();
                } else {
                    currentLine += c;
                }
            }
        }

        void flushLine()
        {
            act.result(resPostBuildLogLine, currentLine);
            currentLine.clear();
        }

        ~LogSink()
        {
            if (currentLine != "") {
                currentLine += '\n';
                flushLine();
            }
        }
    };

    LogSink sink(act);

    runProgram2({
        .program = settings.postBuildHook,
        .environment = hookEnvironment,
        .standardOut = &sink,
        .mergeStderrToStdout = true,
    });
}

BuildError DerivationBuildingGoal::fixupBuilderFailureErrorMessage(BuilderFailureError e)
{
    auto msg =
        fmt("Cannot build '%s'.\n"
            "Reason: " ANSI_RED "builder %s" ANSI_NORMAL ".",
            Magenta(worker.store.printStorePath(drvPath)),
            statusToString(e.builderStatus));

    msg += showKnownOutputs(worker.store, *drv);

    if (!logger->isVerbose() && !logTail.empty()) {
        msg += fmt("\nLast %d log lines:\n", logTail.size());
        for (auto & line : logTail) {
            msg += "> ";
            msg += line;
            msg += "\n";
        }
        auto nixLogCommand = experimentalFeatureSettings.isEnabled(Xp::NixCommand) ? "nix log" : "nix-store -l";
        // The command is on a separate line for easy copying, such as with triple click.
        // This message will be indented elsewhere, so removing the indentation before the
        // command will not put it at the start of the line unfortunately.
        msg +=
            fmt("For full logs, run:\n  " ANSI_BOLD "%s %s" ANSI_NORMAL,
                nixLogCommand,
                worker.store.printStorePath(drvPath));
    }

    msg += e.extraMsgAfter;

    return BuildError{e.status, msg};
}

HookReply DerivationBuildingGoal::tryBuildHook(const std::map<std::string, InitialOutput> & initialOutputs)
{
#ifdef _WIN32 // TODO enable build hook on Windows
    return rpDecline;
#else
    /* This should use `worker.evalStore`, but per #13179 the build hook
       doesn't work with eval store anyways. */
    if (settings.buildHook.get().empty() || !worker.tryBuildHook || !worker.store.isValidPath(drvPath))
        return rpDecline;

    if (!worker.hook)
        worker.hook = std::make_unique<HookInstance>();

    try {

        /* Send the request to the hook. */
        worker.hook->sink << "try" << (worker.getNrLocalBuilds() < settings.maxBuildJobs ? 1 : 0) << drv->platform
                          << worker.store.printStorePath(drvPath) << drvOptions->getRequiredSystemFeatures(*drv);
        worker.hook->sink.flush();

        /* Read the first line of input, which should be a word indicating
           whether the hook wishes to perform the build. */
        std::string reply;
        while (true) {
            auto s = [&]() {
                try {
                    return readLine(worker.hook->fromHook.readSide.get());
                } catch (Error & e) {
                    e.addTrace({}, "while reading the response from the build hook");
                    throw;
                }
            }();
            if (handleJSONLogMessage(s, worker.act, worker.hook->activities, "the build hook", true))
                ;
            else if (s.substr(0, 2) == "# ") {
                reply = s.substr(2);
                break;
            } else {
                s += "\n";
                writeToStderr(s);
            }
        }

        debug("hook reply is '%1%'", reply);

        if (reply == "decline")
            return rpDecline;
        else if (reply == "decline-permanently") {
            worker.tryBuildHook = false;
            worker.hook = 0;
            return rpDecline;
        } else if (reply == "postpone")
            return rpPostpone;
        else if (reply != "accept")
            throw Error("bad hook reply '%s'", reply);

    } catch (SysError & e) {
        if (e.errNo == EPIPE) {
            printError("build hook died unexpectedly: %s", chomp(drainFD(worker.hook->fromHook.readSide.get())));
            worker.hook = 0;
            return rpDecline;
        } else
            throw;
    }

    hook = std::move(worker.hook);

    try {
        hook->machineName = readLine(hook->fromHook.readSide.get());
    } catch (Error & e) {
        e.addTrace({}, "while reading the machine name from the build hook");
        throw;
    }

    CommonProto::WriteConn conn{hook->sink};

    /* Tell the hook all the inputs that have to be copied to the
       remote system. */
    CommonProto::write(worker.store, conn, inputPaths);

    /* Tell the hooks the missing outputs that have to be copied back
       from the remote system. */
    {
        StringSet missingOutputs;
        for (auto & [outputName, status] : initialOutputs) {
            // XXX: Does this include known CA outputs?
            if (buildMode != bmCheck && status.known && status.known->isValid())
                continue;
            missingOutputs.insert(outputName);
        }
        CommonProto::write(worker.store, conn, missingOutputs);
    }

    hook->sink = FdSink();
    hook->toHook.writeSide.close();

    /* Create the log file and pipe. */
    [[maybe_unused]] Path logFile = openLogFile();

    std::set<MuxablePipePollState::CommChannel> fds;
    fds.insert(hook->fromHook.readSide.get());
    fds.insert(hook->builderOut.readSide.get());
    worker.childStarted(shared_from_this(), fds, false, false);

    return rpAccept;
#endif
}

Path DerivationBuildingGoal::openLogFile()
{
    logSize = 0;

    if (!settings.keepLog)
        return "";

    auto baseName = std::string(baseNameOf(worker.store.printStorePath(drvPath)));

    /* Create a log file. */
    Path logDir;
    if (auto localStore = dynamic_cast<LocalStore *>(&worker.store))
        logDir = localStore->config->logDir;
    else
        logDir = settings.nixLogDir;
    Path dir = fmt("%s/%s/%s/", logDir, LocalFSStore::drvsLogDir, baseName.substr(0, 2));
    createDirs(dir);

    Path logFileName = fmt("%s/%s%s", dir, baseName.substr(2), settings.compressLog ? ".bz2" : "");

    fdLogFile = toDescriptor(open(
        logFileName.c_str(),
        O_CREAT | O_WRONLY | O_TRUNC
#ifndef _WIN32
            | O_CLOEXEC
#endif
        ,
        0666));
    if (!fdLogFile)
        throw SysError("creating log file '%1%'", logFileName);

    logFileSink = std::make_shared<FdSink>(fdLogFile.get());

    if (settings.compressLog)
        logSink = std::shared_ptr<CompressionSink>(makeCompressionSink("bzip2", *logFileSink));
    else
        logSink = logFileSink;

    return logFileName;
}

void DerivationBuildingGoal::closeLogFile()
{
    auto logSink2 = std::dynamic_pointer_cast<CompressionSink>(logSink);
    if (logSink2)
        logSink2->finish();
    if (logFileSink)
        logFileSink->flush();
    logSink = logFileSink = 0;
    fdLogFile.close();
}

bool DerivationBuildingGoal::isReadDesc(Descriptor fd)
{
#ifdef _WIN32 // TODO enable build hook on Windows
    return false;
#else
    return (hook && fd == hook->builderOut.readSide.get()) || (builder && fd == builder->builderOut.get());
#endif
}

void DerivationBuildingGoal::handleChildOutput(Descriptor fd, std::string_view data)
{
    // local & `ssh://`-builds are dealt with here.
    auto isWrittenToLog = isReadDesc(fd);
    if (isWrittenToLog) {
        logSize += data.size();
        if (settings.maxLogSize && logSize > settings.maxLogSize) {
            killChild();
            // We're not inside a coroutine, hence we can't use co_return here.
            // Thus we ignore the return value.
            [[maybe_unused]] Done _ = doneFailure(BuildError(
                BuildResult::Failure::LogLimitExceeded,
                "%s killed after writing more than %d bytes of log output",
                getName(),
                settings.maxLogSize));
            return;
        }

        for (auto c : data)
            if (c == '\r')
                currentLogLinePos = 0;
            else if (c == '\n')
                flushLine();
            else {
                if (currentLogLinePos >= currentLogLine.size())
                    currentLogLine.resize(currentLogLinePos + 1);
                currentLogLine[currentLogLinePos++] = c;
            }

        if (logSink)
            (*logSink)(data);
    }

#ifndef _WIN32 // TODO enable build hook on Windows
    if (hook && fd == hook->fromHook.readSide.get()) {
        for (auto c : data)
            if (c == '\n') {
                auto json = parseJSONMessage(currentHookLine, "the derivation builder");
                if (json) {
                    auto s = handleJSONLogMessage(*json, worker.act, hook->activities, "the derivation builder", true);
                    // ensure that logs from a builder using `ssh-ng://` as protocol
                    // are also available to `nix log`.
                    if (s && !isWrittenToLog && logSink) {
                        const auto type = (*json)["type"];
                        const auto fields = (*json)["fields"];
                        if (type == resBuildLogLine) {
                            (*logSink)((fields.size() > 0 ? fields[0].get<std::string>() : "") + "\n");
                        } else if (type == resSetPhase && !fields.is_null()) {
                            const auto phase = fields[0];
                            if (!phase.is_null()) {
                                // nixpkgs' stdenv produces lines in the log to signal
                                // phase changes.
                                // We want to get the same lines in case of remote builds.
                                // The format is:
                                //   @nix { "action": "setPhase", "phase": "$curPhase" }
                                const auto logLine = nlohmann::json::object({{"action", "setPhase"}, {"phase", phase}});
                                (*logSink)(
                                    "@nix " + logLine.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace)
                                    + "\n");
                            }
                        }
                    }
                }
                currentHookLine.clear();
            } else
                currentHookLine += c;
    }
#endif
}

void DerivationBuildingGoal::handleEOF(Descriptor fd)
{
    if (!currentLogLine.empty())
        flushLine();
    worker.wakeUp(shared_from_this());
}

void DerivationBuildingGoal::flushLine()
{
    if (handleJSONLogMessage(currentLogLine, *act, builderActivities, "the derivation builder", false))
        ;

    else {
        logTail.push_back(currentLogLine);
        if (logTail.size() > settings.logLines)
            logTail.pop_front();

        act->result(resBuildLogLine, currentLogLine);
    }

    currentLogLine = "";
    currentLogLinePos = 0;
}

std::map<std::string, std::optional<StorePath>> DerivationBuildingGoal::queryPartialDerivationOutputMap()
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

std::pair<bool, SingleDrvOutputs>
DerivationBuildingGoal::checkPathValidity(std::map<std::string, InitialOutput> & initialOutputs)
{
    if (drv->type().isImpure())
        return {false, {}};

    bool checkHash = buildMode == bmRepair;
    SingleDrvOutputs validOutputs;

    for (auto & i : queryPartialDerivationOutputMap()) {
        auto initialOutput = get(initialOutputs, i.first);
        if (!initialOutput)
            // this is an invalid output, gets caught with (!wantedOutputsLeft.empty())
            continue;
        auto & info = *initialOutput;
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
                        {
                            .outPath = info.known->path,
                        },
                        drvOutput,
                    });
            }
        }
        if (info.known && info.known->isValid())
            validOutputs.emplace(
                i.first,
                Realisation{
                    {
                        .outPath = info.known->path,
                    },
                    drvOutput,
                });
    }

    bool allValid = true;
    for (auto & [_, status] : initialOutputs) {
        if (!status.known || !status.known->isValid()) {
            allValid = false;
            break;
        }
    }

    return {allValid, validOutputs};
}

Goal::Done DerivationBuildingGoal::doneSuccess(BuildResult::Success::Status status, SingleDrvOutputs builtOutputs)
{
    buildResult.inner = BuildResult::Success{
        .status = status,
        .builtOutputs = std::move(builtOutputs),
    };

    mcRunningBuilds.reset();

    if (status == BuildResult::Success::Built)
        worker.doneBuilds++;

    worker.updateProgress();

    return amDone(ecSuccess, std::nullopt);
}

Goal::Done DerivationBuildingGoal::doneFailure(BuildError ex)
{
    buildResult.inner = BuildResult::Failure{
        .status = ex.status,
        .errorMsg = fmt("%s", Uncolored(ex.info().msg)),
    };

    mcRunningBuilds.reset();

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
