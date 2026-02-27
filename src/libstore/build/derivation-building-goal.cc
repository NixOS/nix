#include "nix/store/build/derivation-building-goal.hh"
#include "nix/store/build/derivation-env-desugar.hh"
#ifndef _WIN32 // TODO enable build hook on Windows
#  include "nix/store/build/hook-instance.hh"
#  include "nix/store/build/derivation-builder.hh"
#endif
#include "nix/util/fun.hh"
#include "nix/util/processes.hh"
#include "nix/util/environment-variables.hh"
#include "nix/util/config-global.hh"
#include "nix/store/build/worker.hh"
#include "nix/util/util.hh"
#include "nix/util/compression.hh"
#include "nix/store/common-protocol.hh"
#include "nix/store/common-protocol-impl.hh"
#include "nix/store/local-store.hh" // TODO remove, along with remaining downcasts
#include "nix/store/globals.hh"

#include <algorithm>
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
    name = fmt("building derivation '%s'", worker.store.printStorePath(drvPath));
    trace("created");

    /* Prevent the .chroot directory from being
       garbage-collected. (See isActiveTempFile() in gc.cc.) */
    worker.store.addTempRoot(this->drvPath);
}

DerivationBuildingGoal::~DerivationBuildingGoal() = default;

std::string DerivationBuildingGoal::key()
{
    return "dd$" + std::string(drvPath.name()) + "$" + worker.store.printStorePath(drvPath);
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
    const WorkerSettings & workerSettings,
    const StoreDirConfig & store,
    Logger & logger,
    const StorePath & drvPath,
    const StorePathSet & outputPaths);

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
        if (!worker.settings.useSubstitutes)
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
        worker.store.writeDerivation(*drv);
    }

    StorePathSet inputPaths;

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

    debug("added input paths %s", concatMapStringsSep(", ", inputPaths, [&](auto & p) {
              return "'" + worker.store.printStorePath(p) + "'";
          }));

    /* Okay, try to build.  Note that here we don't wait for a build
       slot to become available, since we don't need one if there is a
       build hook. */
    co_await yield();
    co_return tryToBuild(std::move(inputPaths));
}

/**
 * RAII wrapper for build log file.
 * Constructor opens the log file, destructor closes it.
 */
struct LogFile
{
    AutoCloseFD fd;
    std::shared_ptr<BufferedSink> fileSink, sink;

    LogFile(Store & store, const StorePath & drvPath, const LogFileSettings & logSettings);
    ~LogFile();
};

struct LocalBuildRejection
{
    bool maxJobsZero = false;

    struct NoLocalStore
    {};

    /**
     * We have a local store, but we don't have an external derivation builder (which is fine), if we did, it'd be
     * fine because we would not care about platforms and features then. Since we don't, we either have the wrong
     * platform, or we are missing some system features.
     */
    struct WrongLocalStore
    {
        template<typename T>
        struct Pair
        {
            T derivation;
            T localStore;
        };

        std::optional<Pair<std::string>> badPlatform;
        std::optional<Pair<StringSet>> missingFeatures;
    };

    std::variant<NoLocalStore, WrongLocalStore> rejection;
};

static BuildError reject(const LocalBuildRejection & rejection, std::string_view thingCannotBuild)
{
    if (std::get_if<LocalBuildRejection::NoLocalStore>(&rejection.rejection))
        return BuildError(
            BuildResult::Failure::InputRejected,
            "Unable to build with a primary store that isn't a local store; "
            "either pass a different '--store' or enable remote builds.\n\n"
            "For more information check 'man nix.conf' and search for '/machines'.");

    auto & wrongStore = std::get<LocalBuildRejection::WrongLocalStore>(rejection.rejection);

    std::string msg = fmt("Cannot build '%s'.", Magenta(thingCannotBuild));

    if (rejection.maxJobsZero)
        msg += "\nReason: " ANSI_RED "local builds are disabled" ANSI_NORMAL
               " (max-jobs = 0)"
               "\nHint: set 'max-jobs' to a non-zero value to enable local builds, "
               "or configure remote builders via 'builders'";

    if (wrongStore.badPlatform)
        msg +=
            fmt("\nReason: " ANSI_RED "platform mismatch" ANSI_NORMAL
                "\nRequired system: '%s'"
                "\nCurrent system: '%s'",
                Magenta(wrongStore.badPlatform->derivation),
                Magenta(wrongStore.badPlatform->localStore));

    if (wrongStore.missingFeatures)
        msg +=
            fmt("\nReason: " ANSI_RED "missing system features" ANSI_NORMAL
                "\nRequired features: {%s}"
                "\nAvailable features: {%s}",
                concatStringsSep(", ", wrongStore.missingFeatures->derivation),
                concatStringsSep<StringSet>(", ", wrongStore.missingFeatures->localStore));

    if (wrongStore.badPlatform || wrongStore.missingFeatures) {
        // since aarch64-darwin has Rosetta 2, this user can actually run x86_64-darwin on their
        // hardware - we should tell them to run the command to install Rosetta
        if (wrongStore.badPlatform && wrongStore.badPlatform->derivation == "x86_64-darwin"
            && wrongStore.badPlatform->localStore == "aarch64-darwin")
            msg +=
                fmt("\nNote: run `%s` to run programs for x86_64-darwin",
                    Magenta("/usr/sbin/softwareupdate --install-rosetta && launchctl stop org.nixos.nix-daemon"));
    }

    return BuildError(BuildResult::Failure::InputRejected, std::move(msg));
}

Goal::Co DerivationBuildingGoal::tryToBuild(StorePathSet inputPaths)
{
    auto drvOptions = [&] {
        DerivationOptions<SingleDerivedPath> temp;
        try {
            temp =
                derivationOptionsFromStructuredAttrs(worker.store, drv->inputDrvs, drv->env, get(drv->structuredAttrs));
        } catch (Error & e) {
            e.addTrace({}, "while parsing derivation '%s'", worker.store.printStorePath(drvPath));
            throw;
        }

        auto res = tryResolve(
            temp,
            [&](ref<const SingleDerivedPath> drvPath, const std::string & outputName) -> std::optional<StorePath> {
                try {
                    return resolveDerivedPath(
                        worker.store, SingleDerivedPath::Built{drvPath, outputName}, &worker.evalStore);
                } catch (Error &) {
                    return std::nullopt;
                }
            });

        /* The derivation must have all of its inputs gotten this point,
           so the resolution will surely succeed.

           (Actually, we shouldn't even enter this goal until we have a
           resolved derivation, or derivation with only input addressed
           transitive inputs, so this should be a no-opt anyways.)
         */
        assert(res);
        return *res;
    }();

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

    auto localBuildResult = [&]() -> std::variant<LocalBuildCapability, LocalBuildRejection> {
        bool maxJobsZero = worker.settings.maxBuildJobs.get() == 0;

        auto * localStoreP = dynamic_cast<LocalStore *>(&worker.store);
        if (!localStoreP)
            return LocalBuildRejection{.maxJobsZero = maxJobsZero, .rejection = LocalBuildRejection::NoLocalStore{}};

        /**
         * Now that we've decided we can't / won't do a remote build, check
         * that we can in fact build locally. First see if there is an
         * external builder for a "semi-local build". If there is, prefer to
         * use that. If there is not, then check if we can do a "true" local
         * build.
         */
        auto * ext = settings.getLocalSettings().findExternalDerivationBuilderIfSupported(*drv);

        if (ext)
            return LocalBuildCapability{*localStoreP, ext};

        using WrongLocalStore = LocalBuildRejection::WrongLocalStore;

        WrongLocalStore wrongStore;

        if (drv->platform != settings.thisSystem.get() && !settings.extraPlatforms.get().count(drv->platform)
            && !drv->isBuiltin())
            wrongStore.badPlatform = WrongLocalStore::Pair<std::string>{drv->platform, settings.thisSystem.get()};

        {
            auto required = drvOptions.getRequiredSystemFeatures(*drv);
            auto & available = worker.store.config.systemFeatures.get();
            if (std::ranges::any_of(required, [&](const std::string & f) { return !available.count(f); }))
                wrongStore.missingFeatures = WrongLocalStore::Pair<StringSet>{required, available};
        }

        if (maxJobsZero || wrongStore.badPlatform || wrongStore.missingFeatures)
            return LocalBuildRejection{.maxJobsZero = maxJobsZero, .rejection = std::move(wrongStore)};

        return LocalBuildCapability{*localStoreP, ext};
    }();

    auto acquireResources = [&](bool & done, PathLocks & outputLocks) -> Goal::Co {
        trace("trying to build");

        /**
         * Output paths to acquire locks on, if known a priori.
         *
         * The locks are automatically released when the caller's `PathLocks` goes
         * out of scope, including on exception unwinding.  If we can't acquire the lock, then
         * continue; hopefully some other goal can start a build, and if not, the
         * main loop will sleep a few seconds and then retry this goal.
         */
        std::set<std::filesystem::path> lockFiles;
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
                else {
                    auto lockPath = localStore->toRealPath(drvPath);
                    lockPath += "." + i.first;
                    lockFiles.insert(std::move(lockPath));
                }
            }
        }

        if (!outputLocks.lockPaths(lockFiles, "", false)) {
            Activity act(
                *logger,
                lvlWarn,
                actBuildWaiting,
                fmt("waiting for lock on %s",
                    Magenta(concatMapStringsSep(", ", lockFiles, [](auto & p) { return "'" + p.string() + "'"; }))));

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
            done = true;
            co_return Return{};
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

        co_return Return{};
    };

    auto tryHookLoop = [&](bool & valid) -> Goal::Co {
        {
            PathLocks outputLocks;
            co_await acquireResources(valid, outputLocks);
            if (valid)
                co_return doneSuccess(BuildResult::Success::AlreadyValid, checkPathValidity(initialOutputs).second);

            switch (tryBuildHook(drvOptions)) {
            case rpAccept:
                /* Yes, it has started doing so.  Wait until we get
                   EOF from the hook. */
                valid = true;
                co_return buildWithHook(
                    std::move(inputPaths), std::move(initialOutputs), std::move(drvOptions), std::move(outputLocks));
            case rpDecline:
                // We should do it ourselves.
                co_return Return{};
            case rpPostpone:
                /* Not now; wait until at least one child finishes or
                   the wake-up timeout expires. */
                break;
            }
        }

        PathLocks outputLocks;
        {
            // First attempt was postponed. Retry in a loop with an activity
            // that lives until accept or decline.
            Activity act(
                *logger,
                lvlWarn,
                actBuildWaiting,
                fmt("waiting for a machine to build '%s'", Magenta(worker.store.printStorePath(drvPath))));

            while (true) {
                co_await waitForAWhile();
                co_await acquireResources(valid, outputLocks);
                if (valid)
                    break;

                switch (tryBuildHook(drvOptions)) {
                case rpAccept:
                    /* Yes, it has started doing so.  Wait until we get
                       EOF from the hook. */
                    break;
                case rpPostpone:
                    /* Not now; wait until at least one child finishes or
                       the wake-up timeout expires. */
                    outputLocks.unlock();
                    continue;
                case rpDecline:
                    // We should do it ourselves.
                    co_return Return{};
                }

                break;
            }
        }

        if (valid) {
            co_return doneSuccess(BuildResult::Success::AlreadyValid, checkPathValidity(initialOutputs).second);
        } else {
            co_return buildWithHook(
                std::move(inputPaths), std::move(initialOutputs), std::move(drvOptions), std::move(outputLocks));
        }
    };

    auto tryBuildLocally = [&](bool & valid) -> Goal::Co {
        if (auto * cap = std::get_if<LocalBuildCapability>(&localBuildResult)) {
            PathLocks outputLocks;
            co_await acquireResources(valid, outputLocks);
            if (valid)
                co_return doneSuccess(BuildResult::Success::AlreadyValid, checkPathValidity(initialOutputs).second);

            valid = true;
            co_return buildLocally(
                *cap, std::move(inputPaths), std::move(initialOutputs), std::move(drvOptions), std::move(outputLocks));
        }

        co_return Return{};
    };

    if (buildMode != bmNormal) {
        // Check and repair modes operate on the state of this store specifically,
        // so they must always build locally.
        bool valid = false;
        co_await tryBuildLocally(valid);
        if (valid)
            co_return Return{};
    } else if (drvOptions.preferLocalBuild) {
        // Local is preferred, so try it first. If it's not available, fall back to the hook.
        {
            bool valid = false;
            co_await tryBuildLocally(valid);
            if (valid)
                co_return Return{};
        }
        {
            bool valid = false;
            co_await tryHookLoop(valid);
            if (valid)
                co_return Return{};
        }
    } else {
        // Default preference is a remote build: they tend to be faster and preserve local
        // resources for other tasks. Fall back to local if no remote is available.
        {
            bool valid = false;
            co_await tryHookLoop(valid);
            if (valid)
                co_return Return{};
        }
        {
            bool valid = false;
            co_await tryBuildLocally(valid);
            if (valid)
                co_return Return{};
        }
    }

    std::string storePath = worker.store.printStorePath(drvPath);
    auto * rejection = std::get_if<LocalBuildRejection>(&localBuildResult);
    assert(rejection);
    co_return doneFailure(reject(*rejection, storePath));
}

Goal::Co DerivationBuildingGoal::buildWithHook(
    StorePathSet inputPaths,
    std::map<std::string, InitialOutput> initialOutputs,
    DerivationOptions<StorePath> drvOptions,
    PathLocks outputLocks)
{
#ifdef _WIN32 // TODO enable build hook on Windows
    unreachable();
#else
    std::unique_ptr<HookInstance> hook = std::move(worker.hook);

    /* Set up callback so childTerminated is called if the hook is
       destroyed (e.g., during failure cascades). */
    hook->onKillChild = [this]() { worker.childTerminated(this, JobCategory::Build); };

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
    std::unique_ptr<LogFile> logFile = std::make_unique<LogFile>(worker.store, drvPath, settings.getLogFileSettings());

    std::set<MuxablePipePollState::CommChannel> fds;
    fds.insert(hook->fromHook.readSide.get());
    fds.insert(hook->builderOut.readSide.get());
    worker.childStarted(shared_from_this(), fds, false, false);

    buildResult.startTime = time(nullptr); // inexact

    auto msg =
        fmt(buildMode == bmRepair  ? "repairing outputs of '%s'"
            : buildMode == bmCheck ? "checking outputs of '%s'"
                                   : "building '%s'",
            worker.store.printStorePath(drvPath));
    msg += fmt(" on '%s'", hook->machineName);

    std::unique_ptr<BuildLog> buildLog = std::make_unique<BuildLog>(
        worker.settings.logLines,
        std::make_unique<Activity>(
            *logger,
            lvlInfo,
            actBuild,
            msg,
            Logger::Fields{worker.store.printStorePath(drvPath), hook->machineName, 1, 1}));
    mcRunningBuilds = std::make_unique<MaintainCount<uint64_t>>(worker.runningBuilds);
    worker.updateProgress();

    std::string currentHookLine;
    uint64_t logSize = 0;

    while (true) {
        auto event = co_await WaitForChildEvent{};
        if (auto * output = std::get_if<ChildOutput>(&event)) {
            auto & fd = output->fd;
            auto & data = output->data;
            if (fd == hook->builderOut.readSide.get()) {
                logSize += data.size();
                if (worker.settings.maxLogSize && logSize > worker.settings.maxLogSize) {
                    hook.reset();
                    co_return doneFailureLogTooLong(*buildLog);
                }
                (*buildLog)(data);
                if (logFile->sink)
                    (*logFile->sink)(data);
            } else if (fd == hook->fromHook.readSide.get()) {
                for (auto c : data)
                    if (c == '\n') {
                        auto json = parseJSONMessage(currentHookLine, "the derivation builder");
                        if (json) {
                            auto s = handleJSONLogMessage(
                                *json, worker.act, hook->activities, "the derivation builder", true);
                            // ensure that logs from a builder using `ssh-ng://` as protocol
                            // are also available to `nix log`.
                            if (s && logFile->sink) {
                                const auto type = (*json)["type"];
                                const auto fields = (*json)["fields"];
                                if (type == resBuildLogLine) {
                                    (*logFile->sink)((fields.size() > 0 ? fields[0].get<std::string>() : "") + "\n");
                                } else if (type == resSetPhase && !fields.is_null()) {
                                    const auto phase = fields[0];
                                    if (!phase.is_null()) {
                                        // nixpkgs' stdenv produces lines in the log to signal
                                        // phase changes.
                                        // We want to get the same lines in case of remote builds.
                                        // The format is:
                                        //   @nix { "action": "setPhase", "phase": "$curPhase" }
                                        const auto logLine =
                                            nlohmann::json::object({{"action", "setPhase"}, {"phase", phase}});
                                        (*logFile->sink)(
                                            "@nix "
                                            + logLine.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace)
                                            + "\n");
                                    }
                                }
                            }
                        }
                        currentHookLine.clear();
                    } else
                        currentHookLine += c;
            }
        } else if (std::get_if<ChildEOF>(&event)) {
            buildLog->flush();
            break;
        } else if (auto * timeout = std::get_if<TimedOut>(&event)) {
            hook.reset();
            co_return doneFailure(std::move(*timeout));
        }
    }

    trace("hook build done");

    /* Since we got an EOF on the logger pipe, the builder is presumed
       to have terminated.  In fact, the builder could also have
       simply have closed its end of the pipe, so just to be sure,
       kill it. */
    int status = hook->pid.kill();

    debug("build hook for '%s' finished", worker.store.printStorePath(drvPath));

    buildResult.timesBuilt++;
    buildResult.stopTime = time(nullptr);

    /* So the child is gone now. */
    worker.childTerminated(this);

    /* Close the read side of the logger pipe. */
    hook->builderOut.readSide.close();
    hook->fromHook.readSide.close();

    /* Close the log file. */
    logFile.reset();

    /* Check the exit status. */
    if (!statusOk(status)) {
        auto e = fixupBuilderFailureErrorMessage({BuildResult::Failure::MiscFailure, status, ""}, *buildLog);

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
    runPostBuildHook(worker.settings, worker.store, *logger, drvPath, outputPaths);

    /* It is now safe to delete the lock files, since all future
       lockers will see that the output paths are valid; they will
       not create new lock files with the same names as the old
       (unlinked) lock files. */
    outputLocks.setDeletion(true);
    outputLocks.unlock();

    co_return doneSuccess(BuildResult::Success::Built, std::move(builtOutputs));
#endif
}

Goal::Co DerivationBuildingGoal::buildLocally(
    LocalBuildCapability localBuildCap,
    StorePathSet inputPaths,
    std::map<std::string, InitialOutput> initialOutputs,
    DerivationOptions<StorePath> drvOptions,
    PathLocks outputLocks)
{
    co_await yield();

#ifdef _WIN32 // TODO enable `DerivationBuilder` on Windows
    throw UnimplementedError("building derivations is not yet implemented on Windows");
#else
    std::unique_ptr<BuildLog> buildLog;
    std::unique_ptr<LogFile> logFile;

    auto openLogFile = [&]() {
        logFile = std::make_unique<LogFile>(worker.store, drvPath, settings.getLogFileSettings());
    };

    auto closeLogFile = [&]() { logFile.reset(); };

    auto started = [&]() {
        auto msg =
            fmt(buildMode == bmRepair  ? "repairing outputs of '%s'"
                : buildMode == bmCheck ? "checking outputs of '%s'"
                                       : "building '%s'",
                worker.store.printStorePath(drvPath));
        buildLog = std::make_unique<BuildLog>(
            worker.settings.logLines,
            std::make_unique<Activity>(
                *logger, lvlInfo, actBuild, msg, Logger::Fields{worker.store.printStorePath(drvPath), "", 1, 1}));
        mcRunningBuilds = std::make_unique<MaintainCount<uint64_t>>(worker.runningBuilds);
        worker.updateProgress();
    };

    std::unique_ptr<Activity> actLock;
    DerivationBuilderUnique builder;
    Descriptor builderOut;

    // Will continue here while waiting for a build user below
    while (true) {

        unsigned int curBuilds = worker.getNrLocalBuilds();
        if (curBuilds >= worker.settings.maxBuildJobs) {
            outputLocks.unlock();
            co_await waitForBuildSlot();
            co_return tryToBuild(std::move(inputPaths));
        }

        if (!builder) {
            /**
             * Local implementation of these virtual methods, consider
             * this just a record of lambdas.
             */
            struct DerivationBuildingGoalCallbacks : DerivationBuilderCallbacks
            {
                DerivationBuildingGoal & goal;
                fun<void()> openLogFileFn;
                fun<void()> closeLogFileFn;

                DerivationBuildingGoalCallbacks(
                    DerivationBuildingGoal & goal, fun<void()> openLogFileFn, fun<void()> closeLogFileFn)
                    : goal{goal}
                    , openLogFileFn{std::move(openLogFileFn)}
                    , closeLogFileFn{std::move(closeLogFileFn)}
                {
                }

                ~DerivationBuildingGoalCallbacks() override = default;

                void childTerminated() override
                {
                    goal.worker.childTerminated(&goal, JobCategory::Build);
                }

                void openLogFile() override
                {
                    openLogFileFn();
                }

                void closeLogFile() override
                {
                    closeLogFileFn();
                }
            };

            decltype(DerivationBuilderParams::defaultPathsInChroot) defaultPathsInChroot =
                localBuildCap.localStore.config->getLocalSettings().sandboxPaths.get();
            DesugaredEnv desugaredEnv;

            /* Add the closure of store paths to the chroot. */
            StorePathSet closure;
            for (auto & i : defaultPathsInChroot)
                try {
                    if (worker.store.isInStore(i.second.source.string()))
                        worker.store.computeFSClosure(
                            worker.store.toStorePath(i.second.source.string()).first, closure);
                } catch (InvalidPath & e) {
                } catch (Error & e) {
                    e.addTrace({}, "while processing sandbox path %s", PathFmt(i.second.source));
                    throw;
                }
            for (auto & i : closure) {
                auto p = worker.store.printStorePath(i);
                defaultPathsInChroot.insert_or_assign(p, ChrootPath{.source = p});
            }

            try {
                desugaredEnv = DesugaredEnv::create(worker.store, *drv, drvOptions, inputPaths);
            } catch (BuildError & e) {
                outputLocks.unlock();
                co_return doneFailure(std::move(e));
            }

            DerivationBuilderParams params{
                .drvPath = drvPath,
                .buildResult = buildResult,
                .drv = *drv,
                .drvOptions = drvOptions,
                .inputPaths = inputPaths,
                .initialOutputs = initialOutputs,
                .buildMode = buildMode,
                .defaultPathsInChroot = std::move(defaultPathsInChroot),
                .systemFeatures = worker.store.config.systemFeatures.get(),
                .desugaredEnv = std::move(desugaredEnv),
            };

            /* If we have to wait and retry (see below), then `builder` will
               already be created, so we don't need to create it again. */
            builder = localBuildCap.externalBuilder
                          ? makeExternalDerivationBuilder(
                                localBuildCap.localStore,
                                std::make_unique<DerivationBuildingGoalCallbacks>(*this, openLogFile, closeLogFile),
                                std::move(params),
                                *localBuildCap.externalBuilder)
                          : makeDerivationBuilder(
                                localBuildCap.localStore,
                                std::make_unique<DerivationBuildingGoalCallbacks>(*this, openLogFile, closeLogFile),
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

    uint64_t logSize = 0;

    while (true) {
        auto event = co_await WaitForChildEvent{};
        if (auto * output = std::get_if<ChildOutput>(&event)) {
            if (output->fd == builder->builderOut.get()) {
                logSize += output->data.size();
                if (worker.settings.maxLogSize && logSize > worker.settings.maxLogSize) {
                    builder->killChild();
                    co_return doneFailureLogTooLong(*buildLog);
                }
                (*buildLog)(output->data);
                if (logFile->sink)
                    (*logFile->sink)(output->data);
            }
        } else if (std::get_if<ChildEOF>(&event)) {
            buildLog->flush();
            break;
        } else if (auto * timeout = std::get_if<TimedOut>(&event)) {
            builder->killChild();
            co_return doneFailure(std::move(*timeout));
        }
    }

    trace("build done");

    SingleDrvOutputs builtOutputs;
    try {
        builtOutputs = builder->unprepareBuild();
    } catch (BuilderFailureError & e) {
        builder.reset();
        outputLocks.unlock();
        co_return doneFailure(fixupBuilderFailureErrorMessage(std::move(e), *buildLog));
    } catch (BuildError & e) {
        builder.reset();
        outputLocks.unlock();
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
        runPostBuildHook(worker.settings, worker.store, *logger, drvPath, outputPaths);

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
    const WorkerSettings & workerSettings,
    const StoreDirConfig & store,
    Logger & logger,
    const StorePath & drvPath,
    const StorePathSet & outputPaths)
{
    auto hook = workerSettings.postBuildHook;
    if (hook == "")
        return;

    Activity act(
        logger,
        lvlTalkative,
        actPostBuildHook,
        fmt("running post-build-hook '%s'", workerSettings.postBuildHook),
        Logger::Fields{store.printStorePath(drvPath)});
    PushActivity pact(act.id);
    OsStringMap hookEnvironment = getEnvOs();

    hookEnvironment.emplace(OS_STR("DRV_PATH"), string_to_os_string(store.printStorePath(drvPath)));
    hookEnvironment.emplace(
        OS_STR("OUT_PATHS"), string_to_os_string(chomp(concatStringsSep(" ", store.printStorePathSet(outputPaths)))));
    hookEnvironment.emplace(OS_STR("NIX_CONFIG"), string_to_os_string(globalConfig.toKeyValue()));

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
        .program = workerSettings.postBuildHook.get(),
        .environment = hookEnvironment,
        .standardOut = &sink,
        .mergeStderrToStdout = true,
    });
}

BuildError DerivationBuildingGoal::fixupBuilderFailureErrorMessage(BuilderFailureError e, BuildLog & buildLog)
{
    auto msg =
        fmt("Cannot build '%s'.\n"
            "Reason: " ANSI_RED "builder %s" ANSI_NORMAL ".",
            Magenta(worker.store.printStorePath(drvPath)),
            statusToString(e.builderStatus));

    msg += showKnownOutputs(worker.store, *drv);

    auto & logTail = buildLog.getTail();
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

HookReply DerivationBuildingGoal::tryBuildHook(const DerivationOptions<StorePath> & drvOptions)
{
#ifdef _WIN32 // TODO enable build hook on Windows
    return rpDecline;
#else
    /* This should use `worker.evalStore`, but per #13179 the build hook
       doesn't work with eval store anyways. */
    if (worker.settings.buildHook.get().empty() || !worker.tryBuildHook || !worker.store.isValidPath(drvPath))
        return rpDecline;

    if (!worker.hook)
        worker.hook = std::make_unique<HookInstance>(worker.settings.buildHook);

    try {

        /* Send the request to the hook. */
        worker.hook->sink << "try" << (worker.getNrLocalBuilds() < worker.settings.maxBuildJobs ? 1 : 0)
                          << drv->platform << worker.store.printStorePath(drvPath)
                          << drvOptions.getRequiredSystemFeatures(*drv);
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

    } catch (SystemError & e) {
        if (e.is(std::errc::broken_pipe)) {
            printError("build hook died unexpectedly: %s", chomp(drainFD(worker.hook->fromHook.readSide.get())));
            worker.hook = 0;
            return rpDecline;
        } else
            throw;
    }

    return rpAccept;
#endif
}

LogFile::LogFile(Store & store, const StorePath & drvPath, const LogFileSettings & logSettings)
{
    if (!logSettings.keepLog)
        return;

    auto baseName = std::string(baseNameOf(store.printStorePath(drvPath)));

    std::filesystem::path logDir;
    if (auto localStore = dynamic_cast<LocalStore *>(&store))
        logDir = localStore->config->logDir.get();
    else
        logDir = logSettings.nixLogDir;
    auto dir = logDir / LocalFSStore::drvsLogDir / baseName.substr(0, 2);
    createDirs(dir);

    auto logFileName = dir / (baseName.substr(2) + (logSettings.compressLog ? ".bz2" : ""));

    fd = openNewFileForWrite(
        logFileName,
        0666,
        {
            .truncateExisting = true,
            .followSymlinksOnTruncate = true, /* FIXME: Probably shouldn't follow symlinks. */
        });
    if (!fd)
        throw SysError("creating log file %1%", PathFmt(logFileName));

    fileSink = std::make_shared<FdSink>(fd.get());

    if (logSettings.compressLog)
        sink = std::shared_ptr<CompressionSink>(makeCompressionSink(CompressionAlgo::bzip2, *fileSink));
    else
        sink = fileSink;
}

LogFile::~LogFile()
{
    try {
        auto sink2 = std::dynamic_pointer_cast<CompressionSink>(sink);
        if (sink2)
            sink2->finish();
        if (fileSink)
            fileSink->flush();
    } catch (...) {
        ignoreExceptionInDestructor();
    }
}

Goal::Done DerivationBuildingGoal::doneFailureLogTooLong(BuildLog & buildLog)
{
    return doneFailure(BuildError(
        BuildResult::Failure::LogLimitExceeded,
        "%s killed after writing more than %d bytes of log output",
        getName(),
        worker.settings.maxLogSize));
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
    mcRunningBuilds.reset();

    if (status == BuildResult::Success::Built)
        worker.doneBuilds++;

    worker.updateProgress();

    return Goal::doneSuccess(
        BuildResult::Success{
            .status = status,
            .builtOutputs = std::move(builtOutputs),
        });
}

Goal::Done DerivationBuildingGoal::doneFailure(BuildError ex)
{
    mcRunningBuilds.reset();

    worker.exitStatusFlags.updateFromStatus(ex.status);
    if (ex.status != BuildResult::Failure::DependencyFailed)
        worker.failedBuilds++;

    worker.updateProgress();

    return Goal::doneFailure(ecFailed, std::move(ex));
}

} // namespace nix
