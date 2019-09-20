#include "build.hh"
#include "builtins.hh"
#include "references.hh"
#include "finally.hh"
#include "util.hh"
#include "archive.hh"
#include "json.hh"
#include "compression.hh"

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/utsname.h>
#include <sys/resource.h>

#if HAVE_STATVFS
#include <sys/statvfs.h>
#endif

/* Includes required for chroot support. */
#if __linux__
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <netinet/ip.h>
#include <sys/personality.h>
#include <sys/mman.h>
#include <sched.h>
#include <sys/param.h>
#include <sys/mount.h>
#include <sys/syscall.h>
#if HAVE_SECCOMP
#include <seccomp.h>
#endif
#define pivot_root(new_root, put_old) (syscall(SYS_pivot_root, new_root, put_old))
#endif

#include <pwd.h>
#include <grp.h>

namespace nix {

void handleDiffHook(uid_t uid, uid_t gid, Path tryA, Path tryB, Path drvPath, Path tmpDir)
{
    auto diffHook = settings.diffHook;
    if (diffHook != "" && settings.runDiffHook) {
        try {
            RunOptions diffHookOptions(diffHook,{tryA, tryB, drvPath, tmpDir});
            diffHookOptions.searchPath = true;
            diffHookOptions.uid = uid;
            diffHookOptions.gid = gid;
            diffHookOptions.chdir = "/";

            auto diffRes = runProgram(diffHookOptions);
            if (!statusOk(diffRes.first))
                throw ExecError(diffRes.first, fmt("diff-hook program '%1%' %2%", diffHook, statusToString(diffRes.first)));

            if (diffRes.second != "")
                printError(chomp(diffRes.second));
        } catch (Error & error) {
            printError("diff hook execution failed: %s", error.what());
        }
    }
}

std::string rewriteStrings(std::string s, const StringRewrites & rewrites)
{
    for (auto & i : rewrites) {
        size_t j = 0;
        while ((j = s.find(i.first, j)) != string::npos)
            s.replace(j, i.first.size(), i.second);
    }
    return s;
}

const Path DerivationGoal::homeDir = "/homeless-shelter";

DerivationGoal::DerivationGoal(const Path & drvPath, const StringSet & wantedOutputs,
    Worker & worker, BuildMode buildMode)
    : Goal(worker)
    , useDerivation(true)
    , drvPath(drvPath)
    , wantedOutputs(wantedOutputs)
    , buildMode(buildMode)
{
    state = &DerivationGoal::getDerivation;
    name = (format("building of '%1%'") % drvPath).str();
    trace("created");

    mcExpectedBuilds = std::make_unique<MaintainCount<uint64_t>>(worker.expectedBuilds);
    worker.updateProgress();
}


DerivationGoal::DerivationGoal(const Path & drvPath, const BasicDerivation & drv,
    Worker & worker, BuildMode buildMode)
    : Goal(worker)
    , useDerivation(false)
    , drvPath(drvPath)
    , buildMode(buildMode)
{
    this->drv = std::unique_ptr<BasicDerivation>(new BasicDerivation(drv));
    state = &DerivationGoal::haveDerivation;
    name = (format("building of %1%") % showPaths(drv.outputPaths())).str();
    trace("created");

    mcExpectedBuilds = std::make_unique<MaintainCount<uint64_t>>(worker.expectedBuilds);
    worker.updateProgress();

    /* Prevent the .chroot directory from being
       garbage-collected. (See isActiveTempFile() in gc.cc.) */
    worker.store.addTempRoot(drvPath);
}


DerivationGoal::~DerivationGoal()
{
    /* Careful: we should never ever throw an exception from a
       destructor. */
    try { killChild(); } catch (...) { ignoreException(); }
    try { deleteTmpDir(false); } catch (...) { ignoreException(); }
    try { closeLogFile(); } catch (...) { ignoreException(); }
}


inline bool DerivationGoal::needsHashRewrite()
{
#if __linux__
    return !useChroot;
#else
    /* Darwin requires hash rewriting even when sandboxing is enabled. */
    return true;
#endif
}


void DerivationGoal::killChild()
{
    if (pid != -1) {
        worker.childTerminated(this);

        if (buildUser) {
            /* If we're using a build user, then there is a tricky
               race condition: if we kill the build user before the
               child has done its setuid() to the build user uid, then
               it won't be killed, and we'll potentially lock up in
               pid.wait().  So also send a conventional kill to the
               child. */
            ::kill(-pid, SIGKILL); /* ignore the result */
            buildUser->kill();
            pid.wait();
        } else
            pid.kill();

        assert(pid == -1);
    }

    hook.reset();
}


void DerivationGoal::timedOut()
{
    killChild();
    done(BuildResult::TimedOut);
}


void DerivationGoal::work()
{
    (this->*state)();
}


void DerivationGoal::addWantedOutputs(const StringSet & outputs)
{
    /* If we already want all outputs, there is nothing to do. */
    if (wantedOutputs.empty()) return;

    if (outputs.empty()) {
        wantedOutputs.clear();
        needRestart = true;
    } else
        for (auto & i : outputs)
            if (wantedOutputs.find(i) == wantedOutputs.end()) {
                wantedOutputs.insert(i);
                needRestart = true;
            }
}


void DerivationGoal::getDerivation()
{
    trace("init");

    /* The first thing to do is to make sure that the derivation
       exists.  If it doesn't, it may be created through a
       substitute. */
    if (buildMode == bmNormal && worker.store.isValidPath(drvPath)) {
        loadDerivation();
        return;
    }

    addWaitee(worker.makeSubstitutionGoal(drvPath));

    state = &DerivationGoal::loadDerivation;
}


void DerivationGoal::loadDerivation()
{
    trace("loading derivation");

    if (nrFailed != 0) {
        printError(format("cannot build missing derivation '%1%'") % drvPath);
        done(BuildResult::MiscFailure);
        return;
    }

    /* `drvPath' should already be a root, but let's be on the safe
       side: if the user forgot to make it a root, we wouldn't want
       things being garbage collected while we're busy. */
    worker.store.addTempRoot(drvPath);

    assert(worker.store.isValidPath(drvPath));

    /* Get the derivation. */
    drv = std::unique_ptr<BasicDerivation>(new Derivation(worker.store.derivationFromPath(drvPath)));

    haveDerivation();
}


void DerivationGoal::haveDerivation()
{
    trace("have derivation");

    retrySubstitution = false;

    for (auto & i : drv->outputs)
        worker.store.addTempRoot(i.second.path);

    /* Check what outputs paths are not already valid. */
    PathSet invalidOutputs = checkPathValidity(false, buildMode == bmRepair);

    /* If they are all valid, then we're done. */
    if (invalidOutputs.size() == 0 && buildMode == bmNormal) {
        done(BuildResult::AlreadyValid);
        return;
    }

    parsedDrv = std::make_unique<ParsedDerivation>(drvPath, *drv);

    /* We are first going to try to create the invalid output paths
       through substitutes.  If that doesn't work, we'll build
       them. */
    if (settings.useSubstitutes && parsedDrv->substitutesAllowed())
        for (auto & i : invalidOutputs)
            addWaitee(worker.makeSubstitutionGoal(i, buildMode == bmRepair ? Repair : NoRepair));

    if (waitees.empty()) /* to prevent hang (no wake-up event) */
        outputsSubstituted();
    else
        state = &DerivationGoal::outputsSubstituted;
}


void DerivationGoal::outputsSubstituted()
{
    trace("all outputs substituted (maybe)");

    if (nrFailed > 0 && nrFailed > nrNoSubstituters + nrIncompleteClosure && !settings.tryFallback) {
        done(BuildResult::TransientFailure, (format("some substitutes for the outputs of derivation '%1%' failed (usually happens due to networking issues); try '--fallback' to build derivation from source ") % drvPath).str());
        return;
    }

    /*  If the substitutes form an incomplete closure, then we should
        build the dependencies of this derivation, but after that, we
        can still use the substitutes for this derivation itself. */
    if (nrIncompleteClosure > 0) retrySubstitution = true;

    nrFailed = nrNoSubstituters = nrIncompleteClosure = 0;

    if (needRestart) {
        needRestart = false;
        haveDerivation();
        return;
    }

    auto nrInvalid = checkPathValidity(false, buildMode == bmRepair).size();
    if (buildMode == bmNormal && nrInvalid == 0) {
        done(BuildResult::Substituted);
        return;
    }
    if (buildMode == bmRepair && nrInvalid == 0) {
        repairClosure();
        return;
    }
    if (buildMode == bmCheck && nrInvalid > 0)
        throw Error(format("some outputs of '%1%' are not valid, so checking is not possible") % drvPath);

    /* Otherwise, at least one of the output paths could not be
       produced using a substitute.  So we have to build instead. */

    /* Make sure checkPathValidity() from now on checks all
       outputs. */
    wantedOutputs = PathSet();

    /* The inputs must be built before we can build this goal. */
    if (useDerivation)
        for (auto & i : dynamic_cast<Derivation *>(drv.get())->inputDrvs)
            addWaitee(worker.makeDerivationGoal(i.first, i.second, buildMode == bmRepair ? bmRepair : bmNormal));

    for (auto & i : drv->inputSrcs) {
        if (worker.store.isValidPath(i)) continue;
        if (!settings.useSubstitutes)
            throw Error(format("dependency '%1%' of '%2%' does not exist, and substitution is disabled")
                % i % drvPath);
        addWaitee(worker.makeSubstitutionGoal(i));
    }

    if (waitees.empty()) /* to prevent hang (no wake-up event) */
        inputsRealised();
    else
        state = &DerivationGoal::inputsRealised;
}


void DerivationGoal::repairClosure()
{
    /* If we're repairing, we now know that our own outputs are valid.
       Now check whether the other paths in the outputs closure are
       good.  If not, then start derivation goals for the derivations
       that produced those outputs. */

    /* Get the output closure. */
    PathSet outputClosure;
    for (auto & i : drv->outputs) {
        if (!wantOutput(i.first, wantedOutputs)) continue;
        worker.store.computeFSClosure(i.second.path, outputClosure);
    }

    /* Filter out our own outputs (which we have already checked). */
    for (auto & i : drv->outputs)
        outputClosure.erase(i.second.path);

    /* Get all dependencies of this derivation so that we know which
       derivation is responsible for which path in the output
       closure. */
    PathSet inputClosure;
    if (useDerivation) worker.store.computeFSClosure(drvPath, inputClosure);
    std::map<Path, Path> outputsToDrv;
    for (auto & i : inputClosure)
        if (isDerivation(i)) {
            Derivation drv = worker.store.derivationFromPath(i);
            for (auto & j : drv.outputs)
                outputsToDrv[j.second.path] = i;
        }

    /* Check each path (slow!). */
    PathSet broken;
    for (auto & i : outputClosure) {
        if (worker.pathContentsGood(i)) continue;
        printError(format("found corrupted or missing path '%1%' in the output closure of '%2%'") % i % drvPath);
        Path drvPath2 = outputsToDrv[i];
        if (drvPath2 == "")
            addWaitee(worker.makeSubstitutionGoal(i, Repair));
        else
            addWaitee(worker.makeDerivationGoal(drvPath2, PathSet(), bmRepair));
    }

    if (waitees.empty()) {
        done(BuildResult::AlreadyValid);
        return;
    }

    state = &DerivationGoal::closureRepaired;
}


void DerivationGoal::closureRepaired()
{
    trace("closure repaired");
    if (nrFailed > 0)
        throw Error(format("some paths in the output closure of derivation '%1%' could not be repaired") % drvPath);
    done(BuildResult::AlreadyValid);
}


void DerivationGoal::inputsRealised()
{
    trace("all inputs realised");

    if (nrFailed != 0) {
        if (!useDerivation)
            throw Error(format("some dependencies of '%1%' are missing") % drvPath);
        printError(
            format("cannot build derivation '%1%': %2% dependencies couldn't be built")
            % drvPath % nrFailed);
        done(BuildResult::DependencyFailed);
        return;
    }

    if (retrySubstitution) {
        haveDerivation();
        return;
    }

    /* Gather information necessary for computing the closure and/or
       running the build hook. */

    /* The outputs are referenceable paths. */
    for (auto & i : drv->outputs) {
        debug(format("building path '%1%'") % i.second.path);
        allPaths.insert(i.second.path);
    }

    /* Determine the full set of input paths. */

    /* First, the input derivations. */
    if (useDerivation)
        for (auto & i : dynamic_cast<Derivation *>(drv.get())->inputDrvs) {
            /* Add the relevant output closures of the input derivation
               `i' as input paths.  Only add the closures of output paths
               that are specified as inputs. */
            assert(worker.store.isValidPath(i.first));
            Derivation inDrv = worker.store.derivationFromPath(i.first);
            for (auto & j : i.second)
                if (inDrv.outputs.find(j) != inDrv.outputs.end())
                    worker.store.computeFSClosure(inDrv.outputs[j].path, inputPaths);
                else
                    throw Error(
                        format("derivation '%1%' requires non-existent output '%2%' from input derivation '%3%'")
                        % drvPath % j % i.first);
        }

    /* Second, the input sources. */
    worker.store.computeFSClosure(drv->inputSrcs, inputPaths);

    debug(format("added input paths %1%") % showPaths(inputPaths));

    allPaths.insert(inputPaths.begin(), inputPaths.end());

    /* Is this a fixed-output derivation? */
    fixedOutput = drv->isFixedOutput();

    /* Don't repeat fixed-output derivations since they're already
       verified by their output hash.*/
    nrRounds = fixedOutput ? 1 : settings.buildRepeat + 1;

    /* Okay, try to build.  Note that here we don't wait for a build
       slot to become available, since we don't need one if there is a
       build hook. */
    state = &DerivationGoal::tryToBuild;
    worker.wakeUp(shared_from_this());

    result = BuildResult();
}


void DerivationGoal::tryToBuild()
{
    trace("trying to build");

    /* Obtain locks on all output paths.  The locks are automatically
       released when we exit this function or Nix crashes.  If we
       can't acquire the lock, then continue; hopefully some other
       goal can start a build, and if not, the main loop will sleep a
       few seconds and then retry this goal. */
    PathSet lockFiles;
    for (auto & outPath : drv->outputPaths())
        lockFiles.insert(worker.store.toRealPath(outPath));

    if (!outputLocks.lockPaths(lockFiles, "", false)) {
        worker.waitForAWhile(shared_from_this());
        return;
    }

    /* Now check again whether the outputs are valid.  This is because
       another process may have started building in parallel.  After
       it has finished and released the locks, we can (and should)
       reuse its results.  (Strictly speaking the first check can be
       omitted, but that would be less efficient.)  Note that since we
       now hold the locks on the output paths, no other process can
       build this derivation, so no further checks are necessary. */
    validPaths = checkPathValidity(true, buildMode == bmRepair);
    if (buildMode != bmCheck && validPaths.size() == drv->outputs.size()) {
        debug(format("skipping build of derivation '%1%', someone beat us to it") % drvPath);
        outputLocks.setDeletion(true);
        done(BuildResult::AlreadyValid);
        return;
    }

    missingPaths = drv->outputPaths();
    if (buildMode != bmCheck)
        for (auto & i : validPaths) missingPaths.erase(i);

    /* If any of the outputs already exist but are not valid, delete
       them. */
    for (auto & i : drv->outputs) {
        Path path = i.second.path;
        if (worker.store.isValidPath(path)) continue;
        debug(format("removing invalid path '%1%'") % path);
        deletePath(worker.store.toRealPath(path));
    }

    /* Don't do a remote build if the derivation has the attribute
       `preferLocalBuild' set.  Also, check and repair modes are only
       supported for local builds. */
    bool buildLocally = buildMode != bmNormal || parsedDrv->willBuildLocally();

    auto started = [&]() {
        auto msg = fmt(
            buildMode == bmRepair ? "repairing outputs of '%s'" :
            buildMode == bmCheck ? "checking outputs of '%s'" :
            nrRounds > 1 ? "building '%s' (round %d/%d)" :
            "building '%s'", drvPath, curRound, nrRounds);
        fmt("building '%s'", drvPath);
        if (hook) msg += fmt(" on '%s'", machineName);
        act = std::make_unique<Activity>(*logger, lvlInfo, actBuild, msg,
            Logger::Fields{drvPath, hook ? machineName : "", curRound, nrRounds});
        mcRunningBuilds = std::make_unique<MaintainCount<uint64_t>>(worker.runningBuilds);
        worker.updateProgress();
    };

    /* Is the build hook willing to accept this job? */
    if (!buildLocally) {
        switch (tryBuildHook()) {
            case rpAccept:
                /* Yes, it has started doing so.  Wait until we get
                   EOF from the hook. */
                result.startTime = time(0); // inexact
                state = &DerivationGoal::buildDone;
                started();
                return;
            case rpPostpone:
                /* Not now; wait until at least one child finishes or
                   the wake-up timeout expires. */
                worker.waitForAWhile(shared_from_this());
                outputLocks.unlock();
                return;
            case rpDecline:
                /* We should do it ourselves. */
                break;
        }
    }

    /* Make sure that we are allowed to start a build.  If this
       derivation prefers to be done locally, do it even if
       maxBuildJobs is 0. */
    unsigned int curBuilds = worker.getNrLocalBuilds();
    if (curBuilds >= settings.maxBuildJobs && !(buildLocally && curBuilds == 0)) {
        worker.waitForBuildSlot(shared_from_this());
        outputLocks.unlock();
        return;
    }

    try {

        /* Okay, we have to build. */
        startBuilder();

    } catch (BuildError & e) {
        printError(e.msg());
        outputLocks.unlock();
        buildUser.reset();
        worker.permanentFailure = true;
        done(BuildResult::InputRejected, e.msg());
        return;
    }

    /* This state will be reached when we get EOF on the child's
       log pipe. */
    state = &DerivationGoal::buildDone;

    started();
}

void replaceValidPath(const Path & storePath, const Path tmpPath)
{
    /* We can't atomically replace storePath (the original) with
       tmpPath (the replacement), so we have to move it out of the
       way first.  We'd better not be interrupted here, because if
       we're repairing (say) Glibc, we end up with a broken system. */
    Path oldPath = (format("%1%.old-%2%-%3%") % storePath % getpid() % random()).str();
    if (pathExists(storePath))
        rename(storePath.c_str(), oldPath.c_str());
    if (rename(tmpPath.c_str(), storePath.c_str()) == -1)
        throw SysError(format("moving '%1%' to '%2%'") % tmpPath % storePath);
    deletePath(oldPath);
}


MakeError(NotDeterministic, BuildError)


void DerivationGoal::buildDone()
{
    trace("build done");

    /* Release the build user at the end of this function. We don't do
       it right away because we don't want another build grabbing this
       uid and then messing around with our output. */
    Finally releaseBuildUser([&]() { buildUser.reset(); });

    /* Since we got an EOF on the logger pipe, the builder is presumed
       to have terminated.  In fact, the builder could also have
       simply have closed its end of the pipe, so just to be sure,
       kill it. */
    int status = hook ? hook->pid.kill() : pid.kill();

    debug(format("builder process for '%1%' finished") % drvPath);

    result.timesBuilt++;
    result.stopTime = time(0);

    /* So the child is gone now. */
    worker.childTerminated(this);

    /* Close the read side of the logger pipe. */
    if (hook) {
        hook->builderOut.readSide = -1;
        hook->fromHook.readSide = -1;
    } else
        builderOut.readSide = -1;

    /* Close the log file. */
    closeLogFile();

    /* When running under a build user, make sure that all processes
       running under that uid are gone.  This is to prevent a
       malicious user from leaving behind a process that keeps files
       open and modifies them after they have been chown'ed to
       root. */
    if (buildUser) buildUser->kill();

    bool diskFull = false;

    try {

        /* Check the exit status. */
        if (!statusOk(status)) {

            /* Heuristically check whether the build failure may have
               been caused by a disk full condition.  We have no way
               of knowing whether the build actually got an ENOSPC.
               So instead, check if the disk is (nearly) full now.  If
               so, we don't mark this build as a permanent failure. */
#if HAVE_STATVFS
            unsigned long long required = 8ULL * 1024 * 1024; // FIXME: make configurable
            struct statvfs st;
            if (statvfs(worker.store.realStoreDir.c_str(), &st) == 0 &&
                (unsigned long long) st.f_bavail * st.f_bsize < required)
                diskFull = true;
            if (statvfs(tmpDir.c_str(), &st) == 0 &&
                (unsigned long long) st.f_bavail * st.f_bsize < required)
                diskFull = true;
#endif

            deleteTmpDir(false);

            /* Move paths out of the chroot for easier debugging of
               build failures. */
            if (useChroot && buildMode == bmNormal)
                for (auto & i : missingPaths)
                    if (pathExists(chrootRootDir + i))
                        rename((chrootRootDir + i).c_str(), i.c_str());

            std::string msg = (format("builder for '%1%' %2%")
                % drvPath % statusToString(status)).str();

            if (!settings.verboseBuild && !logTail.empty()) {
                msg += (format("; last %d log lines:") % logTail.size()).str();
                for (auto & line : logTail)
                    msg += "\n  " + line;
            }

            if (diskFull)
                msg += "\nnote: build failure may have been caused by lack of free disk space";

            throw BuildError(msg);
        }

        /* Compute the FS closure of the outputs and register them as
           being valid. */
        registerOutputs();

        if (settings.postBuildHook != "") {
            Activity act(*logger, lvlInfo, actPostBuildHook,
                fmt("running post-build-hook '%s'", settings.postBuildHook),
                Logger::Fields{drvPath});
            PushActivity pact(act.id);
            auto outputPaths = drv->outputPaths();
            std::map<std::string, std::string> hookEnvironment = getEnv();

            hookEnvironment.emplace("DRV_PATH", drvPath);
            hookEnvironment.emplace("OUT_PATHS", chomp(concatStringsSep(" ", outputPaths)));

            RunOptions opts(settings.postBuildHook, {});
            opts.environment = hookEnvironment;

            struct LogSink : Sink {
                Activity & act;
                std::string currentLine;

                LogSink(Activity & act) : act(act) { }

                void operator() (const unsigned char * data, size_t len) override {
                    for (size_t i = 0; i < len; i++) {
                        auto c = data[i];

                        if (c == '\n') {
                            flushLine();
                        } else {
                            currentLine += c;
                        }
                    }
                }

                void flushLine() {
                    if (settings.verboseBuild) {
                        printError("post-build-hook: " + currentLine);
                    } else {
                        act.result(resPostBuildLogLine, currentLine);
                    }
                    currentLine.clear();
                }

                ~LogSink() {
                    if (currentLine != "") {
                        currentLine += '\n';
                        flushLine();
                    }
                }
            };
            LogSink sink(act);

            opts.standardOut = &sink;
            opts.mergeStderrToStdout = true;
            runProgram2(opts);
        }

        if (buildMode == bmCheck) {
            done(BuildResult::Built);
            return;
        }

        /* Delete unused redirected outputs (when doing hash rewriting). */
        for (auto & i : redirectedOutputs)
            deletePath(i.second);

        /* Delete the chroot (if we were using one). */
        autoDelChroot.reset(); /* this runs the destructor */

        deleteTmpDir(true);

        /* Repeat the build if necessary. */
        if (curRound++ < nrRounds) {
            outputLocks.unlock();
            state = &DerivationGoal::tryToBuild;
            worker.wakeUp(shared_from_this());
            return;
        }

        /* It is now safe to delete the lock files, since all future
           lockers will see that the output paths are valid; they will
           not create new lock files with the same names as the old
           (unlinked) lock files. */
        outputLocks.setDeletion(true);
        outputLocks.unlock();

    } catch (BuildError & e) {
        printError(e.msg());

        outputLocks.unlock();

        BuildResult::Status st = BuildResult::MiscFailure;

        if (hook && WIFEXITED(status) && WEXITSTATUS(status) == 101)
            st = BuildResult::TimedOut;

        else if (hook && (!WIFEXITED(status) || WEXITSTATUS(status) != 100)) {
        }

        else {
            st =
                dynamic_cast<NotDeterministic*>(&e) ? BuildResult::NotDeterministic :
                statusOk(status) ? BuildResult::OutputRejected :
                fixedOutput || diskFull ? BuildResult::TransientFailure :
                BuildResult::PermanentFailure;
        }

        done(st, e.msg());
        return;
    }

    done(BuildResult::Built);
}


HookReply DerivationGoal::tryBuildHook()
{
    if (!worker.tryBuildHook || !useDerivation) return rpDecline;

    if (!worker.hook)
        worker.hook = std::make_unique<HookInstance>();

    try {

        /* Send the request to the hook. */
        worker.hook->sink
            << "try"
            << (worker.getNrLocalBuilds() < settings.maxBuildJobs ? 1 : 0)
            << drv->platform
            << drvPath
            << parsedDrv->getRequiredSystemFeatures();
        worker.hook->sink.flush();

        /* Read the first line of input, which should be a word indicating
           whether the hook wishes to perform the build. */
        string reply;
        while (true) {
            string s = readLine(worker.hook->fromHook.readSide.get());
            if (handleJSONLogMessage(s, worker.act, worker.hook->activities, true))
                ;
            else if (string(s, 0, 2) == "# ") {
                reply = string(s, 2);
                break;
            }
            else {
                s += "\n";
                writeToStderr(s);
            }
        }

        debug(format("hook reply is '%1%'") % reply);

        if (reply == "decline")
            return rpDecline;
        else if (reply == "decline-permanently") {
            worker.tryBuildHook = false;
            worker.hook = 0;
            return rpDecline;
        }
        else if (reply == "postpone")
            return rpPostpone;
        else if (reply != "accept")
            throw Error(format("bad hook reply '%1%'") % reply);

    } catch (SysError & e) {
        if (e.errNo == EPIPE) {
            printError("build hook died unexpectedly: %s",
                chomp(drainFD(worker.hook->fromHook.readSide.get())));
            worker.hook = 0;
            return rpDecline;
        } else
            throw;
    }

    hook = std::move(worker.hook);

    machineName = readLine(hook->fromHook.readSide.get());

    /* Tell the hook all the inputs that have to be copied to the
       remote system. */
    hook->sink << inputPaths;

    /* Tell the hooks the missing outputs that have to be copied back
       from the remote system. */
    hook->sink << missingPaths;

    hook->sink = FdSink();
    hook->toHook.writeSide = -1;

    /* Create the log file and pipe. */
    Path logFile = openLogFile();

    set<int> fds;
    fds.insert(hook->fromHook.readSide.get());
    fds.insert(hook->builderOut.readSide.get());
    worker.childStarted(shared_from_this(), fds, false, false);

    return rpAccept;
}


void chmod_(const Path & path, mode_t mode)
{
    if (chmod(path.c_str(), mode) == -1)
        throw SysError(format("setting permissions on '%1%'") % path);
}


int childEntry(void * arg)
{
    ((DerivationGoal *) arg)->runChild();
    return 1;
}


PathSet DerivationGoal::exportReferences(PathSet storePaths)
{
    PathSet paths;

    for (auto storePath : storePaths) {

        /* Check that the store path is valid. */
        if (!worker.store.isInStore(storePath))
            throw BuildError(format("'exportReferencesGraph' contains a non-store path '%1%'")
                % storePath);

        storePath = worker.store.toStorePath(storePath);

        if (!inputPaths.count(storePath))
            throw BuildError("cannot export references of path '%s' because it is not in the input closure of the derivation", storePath);

        worker.store.computeFSClosure(storePath, paths);
    }

    /* If there are derivations in the graph, then include their
       outputs as well.  This is useful if you want to do things
       like passing all build-time dependencies of some path to a
       derivation that builds a NixOS DVD image. */
    PathSet paths2(paths);

    for (auto & j : paths2) {
        if (isDerivation(j)) {
            Derivation drv = worker.store.derivationFromPath(j);
            for (auto & k : drv.outputs)
                worker.store.computeFSClosure(k.second.path, paths);
        }
    }

    return paths;
}

static std::once_flag dns_resolve_flag;

static void preloadNSS() {
    /* builtin:fetchurl can trigger a DNS lookup, which with glibc can trigger a dynamic library load of
       one of the glibc NSS libraries in a sandboxed child, which will fail unless the library's already
       been loaded in the parent. So we force a lookup of an invalid domain to force the NSS machinery to
       load its lookup libraries in the parent before any child gets a chance to. */
    std::call_once(dns_resolve_flag, []() {
        struct addrinfo *res = NULL;

        if (getaddrinfo("this.pre-initializes.the.dns.resolvers.invalid.", "http", NULL, &res) != 0) {

#if HAVE_STATVFS
#include <sys/statvfs.h>
#endif
            if (res) freeaddrinfo(res);
        }
    });
}

void DerivationGoal::startBuilder()
{
    /* Right platform? */
    if (!parsedDrv->canBuildLocally())
        throw Error("a '%s' with features {%s} is required to build '%s', but I am a '%s' with features {%s}",
            drv->platform,
            concatStringsSep(", ", parsedDrv->getRequiredSystemFeatures()),
            drvPath,
            settings.thisSystem,
            concatStringsSep(", ", settings.systemFeatures));

    if (drv->isBuiltin())
        preloadNSS();

#if __APPLE__
    additionalSandboxProfile = parsedDrv->getStringAttr("__sandboxProfile").value_or("");
#endif

    /* Are we doing a chroot build? */
    {
        auto noChroot = parsedDrv->getBoolAttr("__noChroot");
        if (settings.sandboxMode == smEnabled) {
            if (noChroot)
                throw Error(format("derivation '%1%' has '__noChroot' set, "
                    "but that's not allowed when 'sandbox' is 'true'") % drvPath);
#if __APPLE__
            if (additionalSandboxProfile != "")
                throw Error(format("derivation '%1%' specifies a sandbox profile, "
                    "but this is only allowed when 'sandbox' is 'relaxed'") % drvPath);
#endif
            useChroot = true;
        }
        else if (settings.sandboxMode == smDisabled)
            useChroot = false;
        else if (settings.sandboxMode == smRelaxed)
            useChroot = !fixedOutput && !noChroot;
    }

    if (worker.store.storeDir != worker.store.realStoreDir) {
        #if __linux__
            useChroot = true;
        #else
            throw Error("building using a diverted store is not supported on this platform");
        #endif
    }

    /* If `build-users-group' is not empty, then we have to build as
       one of the members of that group. */
    if (settings.buildUsersGroup != "" && getuid() == 0) {
#if defined(__linux__) || defined(__APPLE__)
        buildUser = std::make_unique<UserLock>();

        /* Make sure that no other processes are executing under this
           uid. */
        buildUser->kill();
#else
        /* Don't know how to block the creation of setuid/setgid
           binaries on this platform. */
        throw Error("build users are not supported on this platform for security reasons");
#endif
    }

    /* Create a temporary directory where the build will take
       place. */
    auto drvName = storePathToName(drvPath);
    tmpDir = createTempDir("", "nix-build-" + drvName, false, false, 0700);

    /* In a sandbox, for determinism, always use the same temporary
       directory. */
#if __linux__
    tmpDirInSandbox = useChroot ? settings.sandboxBuildDir : tmpDir;
#else
    tmpDirInSandbox = tmpDir;
#endif
    chownToBuilder(tmpDir);

    /* Substitute output placeholders with the actual output paths. */
    for (auto & output : drv->outputs)
        inputRewrites[hashPlaceholder(output.first)] = output.second.path;

    /* Construct the environment passed to the builder. */
    initEnv();

    writeStructuredAttrs();

    /* Handle exportReferencesGraph(), if set. */
    if (!parsedDrv->getStructuredAttrs()) {
        /* The `exportReferencesGraph' feature allows the references graph
           to be passed to a builder.  This attribute should be a list of
           pairs [name1 path1 name2 path2 ...].  The references graph of
           each `pathN' will be stored in a text file `nameN' in the
           temporary build directory.  The text files have the format used
           by `nix-store --register-validity'.  However, the deriver
           fields are left empty. */
        string s = get(drv->env, "exportReferencesGraph");
        Strings ss = tokenizeString<Strings>(s);
        if (ss.size() % 2 != 0)
            throw BuildError(format("odd number of tokens in 'exportReferencesGraph': '%1%'") % s);
        for (Strings::iterator i = ss.begin(); i != ss.end(); ) {
            string fileName = *i++;
            checkStoreName(fileName); /* !!! abuse of this function */
            Path storePath = *i++;

            /* Write closure info to <fileName>. */
            writeFile(tmpDir + "/" + fileName,
                worker.store.makeValidityRegistration(
                    exportReferences({storePath}), false, false));
        }
    }

    if (useChroot) {

        /* Allow a user-configurable set of directories from the
           host file system. */
        PathSet dirs = settings.sandboxPaths;
        PathSet dirs2 = settings.extraSandboxPaths;
        dirs.insert(dirs2.begin(), dirs2.end());

        dirsInChroot.clear();

        for (auto i : dirs) {
            if (i.empty()) continue;
            bool optional = false;
            if (i[i.size() - 1] == '?') {
                optional = true;
                i.pop_back();
            }
            size_t p = i.find('=');
            if (p == string::npos)
                dirsInChroot[i] = {i, optional};
            else
                dirsInChroot[string(i, 0, p)] = {string(i, p + 1), optional};
        }
        dirsInChroot[tmpDirInSandbox] = tmpDir;

        /* Add the closure of store paths to the chroot. */
        PathSet closure;
        for (auto & i : dirsInChroot)
            try {
                if (worker.store.isInStore(i.second.source))
                    worker.store.computeFSClosure(worker.store.toStorePath(i.second.source), closure);
            } catch (InvalidPath & e) {
            } catch (Error & e) {
                throw Error(format("while processing 'sandbox-paths': %s") % e.what());
            }
        for (auto & i : closure)
            dirsInChroot[i] = i;

        PathSet allowedPaths = settings.allowedImpureHostPrefixes;

        /* This works like the above, except on a per-derivation level */
        auto impurePaths = parsedDrv->getStringsAttr("__impureHostDeps").value_or(Strings());

        for (auto & i : impurePaths) {
            bool found = false;
            /* Note: we're not resolving symlinks here to prevent
               giving a non-root user info about inaccessible
               files. */
            Path canonI = canonPath(i);
            /* If only we had a trie to do this more efficiently :) luckily, these are generally going to be pretty small */
            for (auto & a : allowedPaths) {
                Path canonA = canonPath(a);
                if (canonI == canonA || isInDir(canonI, canonA)) {
                    found = true;
                    break;
                }
            }
            if (!found)
                throw Error(format("derivation '%1%' requested impure path '%2%', but it was not in allowed-impure-host-deps") % drvPath % i);

            dirsInChroot[i] = i;
        }

#if __linux__
        /* Create a temporary directory in which we set up the chroot
           environment using bind-mounts.  We put it in the Nix store
           to ensure that we can create hard-links to non-directory
           inputs in the fake Nix store in the chroot (see below). */
        chrootRootDir = worker.store.toRealPath(drvPath) + ".chroot";
        deletePath(chrootRootDir);

        /* Clean up the chroot directory automatically. */
        autoDelChroot = std::make_shared<AutoDelete>(chrootRootDir);

        printMsg(lvlChatty, format("setting up chroot environment in '%1%'") % chrootRootDir);

        if (mkdir(chrootRootDir.c_str(), 0750) == -1)
            throw SysError(format("cannot create '%1%'") % chrootRootDir);

        if (buildUser && chown(chrootRootDir.c_str(), 0, buildUser->getGID()) == -1)
            throw SysError(format("cannot change ownership of '%1%'") % chrootRootDir);

        /* Create a writable /tmp in the chroot.  Many builders need
           this.  (Of course they should really respect $TMPDIR
           instead.) */
        Path chrootTmpDir = chrootRootDir + "/tmp";
        createDirs(chrootTmpDir);
        chmod_(chrootTmpDir, 01777);

        /* Create a /etc/passwd with entries for the build user and the
           nobody account.  The latter is kind of a hack to support
           Samba-in-QEMU. */
        createDirs(chrootRootDir + "/etc");

        writeFile(chrootRootDir + "/etc/passwd", fmt(
                "root:x:0:0:Nix build user:%3%:/noshell\n"
                "nixbld:x:%1%:%2%:Nix build user:%3%:/noshell\n"
                "nobody:x:65534:65534:Nobody:/:/noshell\n",
                sandboxUid, sandboxGid, settings.sandboxBuildDir));

        /* Declare the build user's group so that programs get a consistent
           view of the system (e.g., "id -gn"). */
        writeFile(chrootRootDir + "/etc/group",
            (format(
                "root:x:0:\n"
                "nixbld:!:%1%:\n"
                "nogroup:x:65534:\n") % sandboxGid).str());

        /* Create /etc/hosts with localhost entry. */
        if (!fixedOutput)
            writeFile(chrootRootDir + "/etc/hosts", "127.0.0.1 localhost\n::1 localhost\n");

        /* Make the closure of the inputs available in the chroot,
           rather than the whole Nix store.  This prevents any access
           to undeclared dependencies.  Directories are bind-mounted,
           while other inputs are hard-linked (since only directories
           can be bind-mounted).  !!! As an extra security
           precaution, make the fake Nix store only writable by the
           build user. */
        Path chrootStoreDir = chrootRootDir + worker.store.storeDir;
        createDirs(chrootStoreDir);
        chmod_(chrootStoreDir, 01775);

        if (buildUser && chown(chrootStoreDir.c_str(), 0, buildUser->getGID()) == -1)
            throw SysError(format("cannot change ownership of '%1%'") % chrootStoreDir);

        for (auto & i : inputPaths) {
            Path r = worker.store.toRealPath(i);
            struct stat st;
            if (lstat(r.c_str(), &st))
                throw SysError(format("getting attributes of path '%1%'") % i);
            if (S_ISDIR(st.st_mode))
                dirsInChroot[i] = r;
            else {
                Path p = chrootRootDir + i;
                debug("linking '%1%' to '%2%'", p, r);
                if (link(r.c_str(), p.c_str()) == -1) {
                    /* Hard-linking fails if we exceed the maximum
                       link count on a file (e.g. 32000 of ext3),
                       which is quite possible after a `nix-store
                       --optimise'. */
                    if (errno != EMLINK)
                        throw SysError(format("linking '%1%' to '%2%'") % p % i);
                    StringSink sink;
                    dumpPath(r, sink);
                    StringSource source(*sink.s);
                    restorePath(p, source);
                }
            }
        }

        /* If we're repairing, checking or rebuilding part of a
           multiple-outputs derivation, it's possible that we're
           rebuilding a path that is in settings.dirsInChroot
           (typically the dependencies of /bin/sh).  Throw them
           out. */
        for (auto & i : drv->outputs)
            dirsInChroot.erase(i.second.path);

#elif __APPLE__
        /* We don't really have any parent prep work to do (yet?)
           All work happens in the child, instead. */
#else
        throw Error("sandboxing builds is not supported on this platform");
#endif
    }

    if (needsHashRewrite()) {

        if (pathExists(homeDir))
            throw Error(format("directory '%1%' exists; please remove it") % homeDir);

        /* We're not doing a chroot build, but we have some valid
           output paths.  Since we can't just overwrite or delete
           them, we have to do hash rewriting: i.e. in the
           environment/arguments passed to the build, we replace the
           hashes of the valid outputs with unique dummy strings;
           after the build, we discard the redirected outputs
           corresponding to the valid outputs, and rewrite the
           contents of the new outputs to replace the dummy strings
           with the actual hashes. */
        if (validPaths.size() > 0)
            for (auto & i : validPaths)
                addHashRewrite(i);

        /* If we're repairing, then we don't want to delete the
           corrupt outputs in advance.  So rewrite them as well. */
        if (buildMode == bmRepair)
            for (auto & i : missingPaths)
                if (worker.store.isValidPath(i) && pathExists(i)) {
                    addHashRewrite(i);
                    redirectedBadOutputs.insert(i);
                }
    }

    if (useChroot && settings.preBuildHook != "" && dynamic_cast<Derivation *>(drv.get())) {
        printMsg(lvlChatty, format("executing pre-build hook '%1%'")
            % settings.preBuildHook);
        auto args = useChroot ? Strings({drvPath, chrootRootDir}) :
            Strings({ drvPath });
        enum BuildHookState {
            stBegin,
            stExtraChrootDirs
        };
        auto state = stBegin;
        auto lines = runProgram(settings.preBuildHook, false, args);
        auto lastPos = std::string::size_type{0};
        for (auto nlPos = lines.find('\n'); nlPos != string::npos;
                nlPos = lines.find('\n', lastPos)) {
            auto line = std::string{lines, lastPos, nlPos - lastPos};
            lastPos = nlPos + 1;
            if (state == stBegin) {
                if (line == "extra-sandbox-paths" || line == "extra-chroot-dirs") {
                    state = stExtraChrootDirs;
                } else {
                    throw Error(format("unknown pre-build hook command '%1%'")
                        % line);
                }
            } else if (state == stExtraChrootDirs) {
                if (line == "") {
                    state = stBegin;
                } else {
                    auto p = line.find('=');
                    if (p == string::npos)
                        dirsInChroot[line] = line;
                    else
                        dirsInChroot[string(line, 0, p)] = string(line, p + 1);
                }
            }
        }
    }

    /* Run the builder. */
    printMsg(lvlChatty, format("executing builder '%1%'") % drv->builder);

    /* Create the log file. */
    Path logFile = openLogFile();

    /* Create a pipe to get the output of the builder. */
    //builderOut.create();

    builderOut.readSide = posix_openpt(O_RDWR | O_NOCTTY);
    if (!builderOut.readSide)
        throw SysError("opening pseudoterminal master");

    std::string slaveName(ptsname(builderOut.readSide.get()));

    if (buildUser) {
        if (chmod(slaveName.c_str(), 0600))
            throw SysError("changing mode of pseudoterminal slave");

        if (chown(slaveName.c_str(), buildUser->getUID(), 0))
            throw SysError("changing owner of pseudoterminal slave");
    } else {
        if (grantpt(builderOut.readSide.get()))
            throw SysError("granting access to pseudoterminal slave");
    }

    #if 0
    // Mount the pt in the sandbox so that the "tty" command works.
    // FIXME: this doesn't work with the new devpts in the sandbox.
    if (useChroot)
        dirsInChroot[slaveName] = {slaveName, false};
    #endif

    if (unlockpt(builderOut.readSide.get()))
        throw SysError("unlocking pseudoterminal");

    builderOut.writeSide = open(slaveName.c_str(), O_RDWR | O_NOCTTY);
    if (!builderOut.writeSide)
        throw SysError("opening pseudoterminal slave");

    // Put the pt into raw mode to prevent \n -> \r\n translation.
    struct termios term;
    if (tcgetattr(builderOut.writeSide.get(), &term))
        throw SysError("getting pseudoterminal attributes");

    cfmakeraw(&term);

    if (tcsetattr(builderOut.writeSide.get(), TCSANOW, &term))
        throw SysError("putting pseudoterminal into raw mode");

    result.startTime = time(0);

    /* Fork a child to build the package. */
    ProcessOptions options;

#if __linux__
    if (useChroot) {
        /* Set up private namespaces for the build:

           - The PID namespace causes the build to start as PID 1.
             Processes outside of the chroot are not visible to those
             on the inside, but processes inside the chroot are
             visible from the outside (though with different PIDs).

           - The private mount namespace ensures that all the bind
             mounts we do will only show up in this process and its
             children, and will disappear automatically when we're
             done.

           - The private network namespace ensures that the builder
             cannot talk to the outside world (or vice versa).  It
             only has a private loopback interface. (Fixed-output
             derivations are not run in a private network namespace
             to allow functions like fetchurl to work.)

           - The IPC namespace prevents the builder from communicating
             with outside processes using SysV IPC mechanisms (shared
             memory, message queues, semaphores).  It also ensures
             that all IPC objects are destroyed when the builder
             exits.

           - The UTS namespace ensures that builders see a hostname of
             localhost rather than the actual hostname.

           We use a helper process to do the clone() to work around
           clone() being broken in multi-threaded programs due to
           at-fork handlers not being run. Note that we use
           CLONE_PARENT to ensure that the real builder is parented to
           us.
        */

        if (!fixedOutput)
            privateNetwork = true;

        userNamespaceSync.create();

        options.allowVfork = false;

        Pid helper = startProcess([&]() {

            /* Drop additional groups here because we can't do it
               after we've created the new user namespace.  FIXME:
               this means that if we're not root in the parent
               namespace, we can't drop additional groups; they will
               be mapped to nogroup in the child namespace. There does
               not seem to be a workaround for this. (But who can tell
               from reading user_namespaces(7)?)
               See also https://lwn.net/Articles/621612/. */
            if (getuid() == 0 && setgroups(0, 0) == -1)
                throw SysError("setgroups failed");

            size_t stackSize = 1 * 1024 * 1024;
            char * stack = (char *) mmap(0, stackSize,
                PROT_WRITE | PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
            if (stack == MAP_FAILED) throw SysError("allocating stack");

            int flags = CLONE_NEWUSER | CLONE_NEWPID | CLONE_NEWNS | CLONE_NEWIPC | CLONE_NEWUTS | CLONE_PARENT | SIGCHLD;
            if (privateNetwork)
                flags |= CLONE_NEWNET;

            pid_t child = clone(childEntry, stack + stackSize, flags, this);
            if (child == -1 && errno == EINVAL) {
                /* Fallback for Linux < 2.13 where CLONE_NEWPID and
                   CLONE_PARENT are not allowed together. */
                flags &= ~CLONE_NEWPID;
                child = clone(childEntry, stack + stackSize, flags, this);
            }
            if (child == -1 && (errno == EPERM || errno == EINVAL)) {
                /* Some distros patch Linux to not allow unpriveleged
                 * user namespaces. If we get EPERM or EINVAL, try
                 * without CLONE_NEWUSER and see if that works.
                 */
                flags &= ~CLONE_NEWUSER;
                child = clone(childEntry, stack + stackSize, flags, this);
            }
            /* Otherwise exit with EPERM so we can handle this in the
               parent. This is only done when sandbox-fallback is set
               to true (the default). */
            if (child == -1 && (errno == EPERM || errno == EINVAL) && settings.sandboxFallback)
                _exit(1);
            if (child == -1) throw SysError("cloning builder process");

            writeFull(builderOut.writeSide.get(), std::to_string(child) + "\n");
            _exit(0);
        }, options);

        int res = helper.wait();
        if (res != 0 && settings.sandboxFallback) {
            useChroot = false;
            tmpDirInSandbox = tmpDir;
            goto fallback;
        } else if (res != 0)
            throw Error("unable to start build process");

        userNamespaceSync.readSide = -1;

        pid_t tmp;
        if (!string2Int<pid_t>(readLine(builderOut.readSide.get()), tmp)) abort();
        pid = tmp;

        /* Set the UID/GID mapping of the builder's user namespace
           such that the sandbox user maps to the build user, or to
           the calling user (if build users are disabled). */
        uid_t hostUid = buildUser ? buildUser->getUID() : getuid();
        uid_t hostGid = buildUser ? buildUser->getGID() : getgid();

        writeFile("/proc/" + std::to_string(pid) + "/uid_map",
            (format("%d %d 1") % sandboxUid % hostUid).str());

        writeFile("/proc/" + std::to_string(pid) + "/setgroups", "deny");

        writeFile("/proc/" + std::to_string(pid) + "/gid_map",
            (format("%d %d 1") % sandboxGid % hostGid).str());

        /* Signal the builder that we've updated its user
           namespace. */
        writeFull(userNamespaceSync.writeSide.get(), "1");
        userNamespaceSync.writeSide = -1;

    } else
#endif
    {
    fallback:
        options.allowVfork = !buildUser && !drv->isBuiltin();
        pid = startProcess([&]() {
            runChild();
        }, options);
    }

    /* parent */
    pid.setSeparatePG(true);
    builderOut.writeSide = -1;
    worker.childStarted(shared_from_this(), {builderOut.readSide.get()}, true, true);

    /* Check if setting up the build environment failed. */
    while (true) {
        string msg = readLine(builderOut.readSide.get());
        if (string(msg, 0, 1) == "\1") {
            if (msg.size() == 1) break;
            throw Error(string(msg, 1));
        }
        debug(msg);
    }
}


void DerivationGoal::initEnv()
{
    env.clear();

    /* Most shells initialise PATH to some default (/bin:/usr/bin:...) when
       PATH is not set.  We don't want this, so we fill it in with some dummy
       value. */
    env["PATH"] = "/path-not-set";

    /* Set HOME to a non-existing path to prevent certain programs from using
       /etc/passwd (or NIS, or whatever) to locate the home directory (for
       example, wget looks for ~/.wgetrc).  I.e., these tools use /etc/passwd
       if HOME is not set, but they will just assume that the settings file
       they are looking for does not exist if HOME is set but points to some
       non-existing path. */
    env["HOME"] = homeDir;

    /* Tell the builder where the Nix store is.  Usually they
       shouldn't care, but this is useful for purity checking (e.g.,
       the compiler or linker might only want to accept paths to files
       in the store or in the build directory). */
    env["NIX_STORE"] = worker.store.storeDir;

    /* The maximum number of cores to utilize for parallel building. */
    env["NIX_BUILD_CORES"] = (format("%d") % settings.buildCores).str();

    /* In non-structured mode, add all bindings specified in the
       derivation via the environment, except those listed in the
       passAsFile attribute. Those are passed as file names pointing
       to temporary files containing the contents. Note that
       passAsFile is ignored in structure mode because it's not
       needed (attributes are not passed through the environment, so
       there is no size constraint). */
    if (!parsedDrv->getStructuredAttrs()) {

        StringSet passAsFile = tokenizeString<StringSet>(get(drv->env, "passAsFile"));
        int fileNr = 0;
        for (auto & i : drv->env) {
            if (passAsFile.find(i.first) == passAsFile.end()) {
                env[i.first] = i.second;
            } else {
                string fn = ".attr-" + std::to_string(fileNr++);
                Path p = tmpDir + "/" + fn;
                writeFile(p, i.second);
                chownToBuilder(p);
                env[i.first + "Path"] = tmpDirInSandbox + "/" + fn;
            }
        }

    }

    /* For convenience, set an environment pointing to the top build
       directory. */
    env["NIX_BUILD_TOP"] = tmpDirInSandbox;

    /* Also set TMPDIR and variants to point to this directory. */
    env["TMPDIR"] = env["TEMPDIR"] = env["TMP"] = env["TEMP"] = tmpDirInSandbox;

    /* Explicitly set PWD to prevent problems with chroot builds.  In
       particular, dietlibc cannot figure out the cwd because the
       inode of the current directory doesn't appear in .. (because
       getdents returns the inode of the mount point). */
    env["PWD"] = tmpDirInSandbox;

    /* Compatibility hack with Nix <= 0.7: if this is a fixed-output
       derivation, tell the builder, so that for instance `fetchurl'
       can skip checking the output.  On older Nixes, this environment
       variable won't be set, so `fetchurl' will do the check. */
    if (fixedOutput) env["NIX_OUTPUT_CHECKED"] = "1";

    /* *Only* if this is a fixed-output derivation, propagate the
       values of the environment variables specified in the
       `impureEnvVars' attribute to the builder.  This allows for
       instance environment variables for proxy configuration such as
       `http_proxy' to be easily passed to downloaders like
       `fetchurl'.  Passing such environment variables from the caller
       to the builder is generally impure, but the output of
       fixed-output derivations is by definition pure (since we
       already know the cryptographic hash of the output). */
    if (fixedOutput) {
        for (auto & i : parsedDrv->getStringsAttr("impureEnvVars").value_or(Strings()))
            env[i] = getEnv(i);
    }

    /* Currently structured log messages piggyback on stderr, but we
       may change that in the future. So tell the builder which file
       descriptor to use for that. */
    env["NIX_LOG_FD"] = "2";

    /* Trigger colored output in various tools. */
    env["TERM"] = "xterm-256color";
}


static std::regex shVarName("[A-Za-z_][A-Za-z0-9_]*");


void DerivationGoal::writeStructuredAttrs()
{
    auto & structuredAttrs = parsedDrv->getStructuredAttrs();
    if (!structuredAttrs) return;

    auto json = *structuredAttrs;

    /* Add an "outputs" object containing the output paths. */
    nlohmann::json outputs;
    for (auto & i : drv->outputs)
        outputs[i.first] = rewriteStrings(i.second.path, inputRewrites);
    json["outputs"] = outputs;

    /* Handle exportReferencesGraph. */
    auto e = json.find("exportReferencesGraph");
    if (e != json.end() && e->is_object()) {
        for (auto i = e->begin(); i != e->end(); ++i) {
            std::ostringstream str;
            {
                JSONPlaceholder jsonRoot(str, true);
                PathSet storePaths;
                for (auto & p : *i)
                    storePaths.insert(p.get<std::string>());
                worker.store.pathInfoToJSON(jsonRoot,
                    exportReferences(storePaths), false, true);
            }
            json[i.key()] = nlohmann::json::parse(str.str()); // urgh
        }
    }

    writeFile(tmpDir + "/.attrs.json", rewriteStrings(json.dump(), inputRewrites));

    /* As a convenience to bash scripts, write a shell file that
       maps all attributes that are representable in bash -
       namely, strings, integers, nulls, Booleans, and arrays and
       objects consisting entirely of those values. (So nested
       arrays or objects are not supported.) */

    auto handleSimpleType = [](const nlohmann::json & value) -> std::optional<std::string> {
        if (value.is_string())
            return shellEscape(value);

        if (value.is_number()) {
            auto f = value.get<float>();
            if (std::ceil(f) == f)
                return std::to_string(value.get<int>());
        }

        if (value.is_null())
            return std::string("''");

        if (value.is_boolean())
            return value.get<bool>() ? std::string("1") : std::string("");

        return {};
    };

    std::string jsonSh;

    for (auto i = json.begin(); i != json.end(); ++i) {

        if (!std::regex_match(i.key(), shVarName)) continue;

        auto & value = i.value();

        auto s = handleSimpleType(value);
        if (s)
            jsonSh += fmt("declare %s=%s\n", i.key(), *s);

        else if (value.is_array()) {
            std::string s2;
            bool good = true;

            for (auto i = value.begin(); i != value.end(); ++i) {
                auto s3 = handleSimpleType(i.value());
                if (!s3) { good = false; break; }
                s2 += *s3; s2 += ' ';
            }

            if (good)
                jsonSh += fmt("declare -a %s=(%s)\n", i.key(), s2);
        }

        else if (value.is_object()) {
            std::string s2;
            bool good = true;

            for (auto i = value.begin(); i != value.end(); ++i) {
                auto s3 = handleSimpleType(i.value());
                if (!s3) { good = false; break; }
                s2 += fmt("[%s]=%s ", shellEscape(i.key()), *s3);
            }

            if (good)
                jsonSh += fmt("declare -A %s=(%s)\n", i.key(), s2);
        }
    }

    writeFile(tmpDir + "/.attrs.sh", rewriteStrings(jsonSh, inputRewrites));
}


void DerivationGoal::chownToBuilder(const Path & path)
{
    if (!buildUser) return;
    if (chown(path.c_str(), buildUser->getUID(), buildUser->getGID()) == -1)
        throw SysError(format("cannot change ownership of '%1%'") % path);
}


void setupSeccomp()
{
#if __linux__
    if (!settings.filterSyscalls) return;
#if HAVE_SECCOMP
    scmp_filter_ctx ctx;

    if (!(ctx = seccomp_init(SCMP_ACT_ALLOW)))
        throw SysError("unable to initialize seccomp mode 2");

    Finally cleanup([&]() {
        seccomp_release(ctx);
    });

    if (nativeSystem == "x86_64-linux" &&
        seccomp_arch_add(ctx, SCMP_ARCH_X86) != 0)
        throw SysError("unable to add 32-bit seccomp architecture");

    if (nativeSystem == "x86_64-linux" &&
        seccomp_arch_add(ctx, SCMP_ARCH_X32) != 0)
        throw SysError("unable to add X32 seccomp architecture");

    if (nativeSystem == "aarch64-linux" &&
        seccomp_arch_add(ctx, SCMP_ARCH_ARM) != 0)
        printError("unable to add ARM seccomp architecture; this may result in spurious build failures if running 32-bit ARM processes");

    /* Prevent builders from creating setuid/setgid binaries. */
    for (int perm : { S_ISUID, S_ISGID }) {
        if (seccomp_rule_add(ctx, SCMP_ACT_ERRNO(EPERM), SCMP_SYS(chmod), 1,
                SCMP_A1(SCMP_CMP_MASKED_EQ, (scmp_datum_t) perm, (scmp_datum_t) perm)) != 0)
            throw SysError("unable to add seccomp rule");

        if (seccomp_rule_add(ctx, SCMP_ACT_ERRNO(EPERM), SCMP_SYS(fchmod), 1,
                SCMP_A1(SCMP_CMP_MASKED_EQ, (scmp_datum_t) perm, (scmp_datum_t) perm)) != 0)
            throw SysError("unable to add seccomp rule");

        if (seccomp_rule_add(ctx, SCMP_ACT_ERRNO(EPERM), SCMP_SYS(fchmodat), 1,
                SCMP_A2(SCMP_CMP_MASKED_EQ, (scmp_datum_t) perm, (scmp_datum_t) perm)) != 0)
            throw SysError("unable to add seccomp rule");
    }

    /* Prevent builders from creating EAs or ACLs. Not all filesystems
       support these, and they're not allowed in the Nix store because
       they're not representable in the NAR serialisation. */
    if (seccomp_rule_add(ctx, SCMP_ACT_ERRNO(ENOTSUP), SCMP_SYS(setxattr), 0) != 0 ||
        seccomp_rule_add(ctx, SCMP_ACT_ERRNO(ENOTSUP), SCMP_SYS(lsetxattr), 0) != 0 ||
        seccomp_rule_add(ctx, SCMP_ACT_ERRNO(ENOTSUP), SCMP_SYS(fsetxattr), 0) != 0)
        throw SysError("unable to add seccomp rule");

    if (seccomp_attr_set(ctx, SCMP_FLTATR_CTL_NNP, settings.allowNewPrivileges ? 0 : 1) != 0)
        throw SysError("unable to set 'no new privileges' seccomp attribute");

    if (seccomp_load(ctx) != 0)
        throw SysError("unable to load seccomp BPF program");
#else
    throw Error(
        "seccomp is not supported on this platform; "
        "you can bypass this error by setting the option 'filter-syscalls' to false, but note that untrusted builds can then create setuid binaries!");
#endif
#endif
}


void DerivationGoal::runChild()
{
    /* Warning: in the child we should absolutely not make any SQLite
       calls! */

    try { /* child */

        commonChildInit(builderOut);

        try {
            setupSeccomp();
        } catch (...) {
            if (buildUser) throw;
        }

        bool setUser = true;

        /* Make the contents of netrc available to builtin:fetchurl
           (which may run under a different uid and/or in a sandbox). */
        std::string netrcData;
        try {
            if (drv->isBuiltin() && drv->builder == "builtin:fetchurl")
                netrcData = readFile(settings.netrcFile);
        } catch (SysError &) { }

#if __linux__
        if (useChroot) {

            userNamespaceSync.writeSide = -1;

            if (drainFD(userNamespaceSync.readSide.get()) != "1")
                throw Error("user namespace initialisation failed");

            userNamespaceSync.readSide = -1;

            if (privateNetwork) {

                /* Initialise the loopback interface. */
                AutoCloseFD fd(socket(PF_INET, SOCK_DGRAM, IPPROTO_IP));
                if (!fd) throw SysError("cannot open IP socket");

                struct ifreq ifr;
                strcpy(ifr.ifr_name, "lo");
                ifr.ifr_flags = IFF_UP | IFF_LOOPBACK | IFF_RUNNING;
                if (ioctl(fd.get(), SIOCSIFFLAGS, &ifr) == -1)
                    throw SysError("cannot set loopback interface flags");
            }

            /* Set the hostname etc. to fixed values. */
            char hostname[] = "localhost";
            if (sethostname(hostname, sizeof(hostname)) == -1)
                throw SysError("cannot set host name");
            char domainname[] = "(none)"; // kernel default
            if (setdomainname(domainname, sizeof(domainname)) == -1)
                throw SysError("cannot set domain name");

            /* Make all filesystems private.  This is necessary
               because subtrees may have been mounted as "shared"
               (MS_SHARED).  (Systemd does this, for instance.)  Even
               though we have a private mount namespace, mounting
               filesystems on top of a shared subtree still propagates
               outside of the namespace.  Making a subtree private is
               local to the namespace, though, so setting MS_PRIVATE
               does not affect the outside world. */
            if (mount(0, "/", 0, MS_REC|MS_PRIVATE, 0) == -1) {
                throw SysError("unable to make '/' private mount");
            }

            /* Bind-mount chroot directory to itself, to treat it as a
               different filesystem from /, as needed for pivot_root. */
            if (mount(chrootRootDir.c_str(), chrootRootDir.c_str(), 0, MS_BIND, 0) == -1)
                throw SysError(format("unable to bind mount '%1%'") % chrootRootDir);

            /* Set up a nearly empty /dev, unless the user asked to
               bind-mount the host /dev. */
            Strings ss;
            if (dirsInChroot.find("/dev") == dirsInChroot.end()) {
                createDirs(chrootRootDir + "/dev/shm");
                createDirs(chrootRootDir + "/dev/pts");
                ss.push_back("/dev/full");
                if (settings.systemFeatures.get().count("kvm") && pathExists("/dev/kvm"))
                    ss.push_back("/dev/kvm");
                ss.push_back("/dev/null");
                ss.push_back("/dev/random");
                ss.push_back("/dev/tty");
                ss.push_back("/dev/urandom");
                ss.push_back("/dev/zero");
                createSymlink("/proc/self/fd", chrootRootDir + "/dev/fd");
                createSymlink("/proc/self/fd/0", chrootRootDir + "/dev/stdin");
                createSymlink("/proc/self/fd/1", chrootRootDir + "/dev/stdout");
                createSymlink("/proc/self/fd/2", chrootRootDir + "/dev/stderr");
            }

            /* Fixed-output derivations typically need to access the
               network, so give them access to /etc/resolv.conf and so
               on. */
            if (fixedOutput) {
                ss.push_back("/etc/resolv.conf");

                // Only use nss functions to resolve hosts and
                // services. Don’t use it for anything else that may
                // be configured for this system. This limits the
                // potential impurities introduced in fixed outputs.
                writeFile(chrootRootDir + "/etc/nsswitch.conf", "hosts: files dns\nservices: files\n");

                ss.push_back("/etc/services");
                ss.push_back("/etc/hosts");
                if (pathExists("/var/run/nscd/socket"))
                    ss.push_back("/var/run/nscd/socket");
            }

            for (auto & i : ss) dirsInChroot.emplace(i, i);

            /* Bind-mount all the directories from the "host"
               filesystem that we want in the chroot
               environment. */
            auto doBind = [&](const Path & source, const Path & target, bool optional = false) {
                debug(format("bind mounting '%1%' to '%2%'") % source % target);
                struct stat st;
                if (stat(source.c_str(), &st) == -1) {
                    if (optional && errno == ENOENT)
                        return;
                    else
                        throw SysError("getting attributes of path '%1%'", source);
                }
                if (S_ISDIR(st.st_mode))
                    createDirs(target);
                else {
                    createDirs(dirOf(target));
                    writeFile(target, "");
                }
                if (mount(source.c_str(), target.c_str(), "", MS_BIND | MS_REC, 0) == -1)
                    throw SysError("bind mount from '%1%' to '%2%' failed", source, target);
            };

            for (auto & i : dirsInChroot) {
                if (i.second.source == "/proc") continue; // backwards compatibility
                doBind(i.second.source, chrootRootDir + i.first, i.second.optional);
            }

            /* Bind a new instance of procfs on /proc. */
            createDirs(chrootRootDir + "/proc");
            if (mount("none", (chrootRootDir + "/proc").c_str(), "proc", 0, 0) == -1)
                throw SysError("mounting /proc");

            /* Mount a new tmpfs on /dev/shm to ensure that whatever
               the builder puts in /dev/shm is cleaned up automatically. */
            if (pathExists("/dev/shm") && mount("none", (chrootRootDir + "/dev/shm").c_str(), "tmpfs", 0,
                    fmt("size=%s", settings.sandboxShmSize).c_str()) == -1)
                throw SysError("mounting /dev/shm");

            /* Mount a new devpts on /dev/pts.  Note that this
               requires the kernel to be compiled with
               CONFIG_DEVPTS_MULTIPLE_INSTANCES=y (which is the case
               if /dev/ptx/ptmx exists). */
            if (pathExists("/dev/pts/ptmx") &&
                !pathExists(chrootRootDir + "/dev/ptmx")
                && !dirsInChroot.count("/dev/pts"))
            {
                if (mount("none", (chrootRootDir + "/dev/pts").c_str(), "devpts", 0, "newinstance,mode=0620") == 0)
                {
                    createSymlink("/dev/pts/ptmx", chrootRootDir + "/dev/ptmx");

                    /* Make sure /dev/pts/ptmx is world-writable.  With some
                       Linux versions, it is created with permissions 0.  */
                    chmod_(chrootRootDir + "/dev/pts/ptmx", 0666);
                } else {
                    if (errno != EINVAL)
                        throw SysError("mounting /dev/pts");
                    doBind("/dev/pts", chrootRootDir + "/dev/pts");
                    doBind("/dev/ptmx", chrootRootDir + "/dev/ptmx");
                }
            }

            /* Do the chroot(). */
            if (chdir(chrootRootDir.c_str()) == -1)
                throw SysError(format("cannot change directory to '%1%'") % chrootRootDir);

            if (mkdir("real-root", 0) == -1)
                throw SysError("cannot create real-root directory");

            if (pivot_root(".", "real-root") == -1)
                throw SysError(format("cannot pivot old root directory onto '%1%'") % (chrootRootDir + "/real-root"));

            if (chroot(".") == -1)
                throw SysError(format("cannot change root directory to '%1%'") % chrootRootDir);

            if (umount2("real-root", MNT_DETACH) == -1)
                throw SysError("cannot unmount real root filesystem");

            if (rmdir("real-root") == -1)
                throw SysError("cannot remove real-root directory");

            /* Switch to the sandbox uid/gid in the user namespace,
               which corresponds to the build user or calling user in
               the parent namespace. */
            if (setgid(sandboxGid) == -1)
                throw SysError("setgid failed");
            if (setuid(sandboxUid) == -1)
                throw SysError("setuid failed");

            setUser = false;
        }
#endif

        if (chdir(tmpDirInSandbox.c_str()) == -1)
            throw SysError(format("changing into '%1%'") % tmpDir);

        /* Close all other file descriptors. */
        closeMostFDs({STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO});

#if __linux__
        /* Change the personality to 32-bit if we're doing an
           i686-linux build on an x86_64-linux machine. */
        struct utsname utsbuf;
        uname(&utsbuf);
        if (drv->platform == "i686-linux" &&
            (settings.thisSystem == "x86_64-linux" ||
             (!strcmp(utsbuf.sysname, "Linux") && !strcmp(utsbuf.machine, "x86_64")))) {
            if (personality(PER_LINUX32) == -1)
                throw SysError("cannot set i686-linux personality");
        }

        /* Impersonate a Linux 2.6 machine to get some determinism in
           builds that depend on the kernel version. */
        if ((drv->platform == "i686-linux" || drv->platform == "x86_64-linux") && settings.impersonateLinux26) {
            int cur = personality(0xffffffff);
            if (cur != -1) personality(cur | 0x0020000 /* == UNAME26 */);
        }

        /* Disable address space randomization for improved
           determinism. */
        int cur = personality(0xffffffff);
        if (cur != -1) personality(cur | ADDR_NO_RANDOMIZE);
#endif

        /* Disable core dumps by default. */
        struct rlimit limit = { 0, RLIM_INFINITY };
        setrlimit(RLIMIT_CORE, &limit);

        // FIXME: set other limits to deterministic values?

        /* Fill in the environment. */
        Strings envStrs;
        for (auto & i : env)
            envStrs.push_back(rewriteStrings(i.first + "=" + i.second, inputRewrites));

        /* If we are running in `build-users' mode, then switch to the
           user we allocated above.  Make sure that we drop all root
           privileges.  Note that above we have closed all file
           descriptors except std*, so that's safe.  Also note that
           setuid() when run as root sets the real, effective and
           saved UIDs. */
        if (setUser && buildUser) {
            /* Preserve supplementary groups of the build user, to allow
               admins to specify groups such as "kvm".  */
            if (!buildUser->getSupplementaryGIDs().empty() &&
                setgroups(buildUser->getSupplementaryGIDs().size(),
                          buildUser->getSupplementaryGIDs().data()) == -1)
                throw SysError("cannot set supplementary groups of build user");

            if (setgid(buildUser->getGID()) == -1 ||
                getgid() != buildUser->getGID() ||
                getegid() != buildUser->getGID())
                throw SysError("setgid failed");

            if (setuid(buildUser->getUID()) == -1 ||
                getuid() != buildUser->getUID() ||
                geteuid() != buildUser->getUID())
                throw SysError("setuid failed");
        }

        /* Fill in the arguments. */
        Strings args;

        const char *builder = "invalid";

        if (drv->isBuiltin()) {
            ;
        }
#if __APPLE__
        else if (getEnv("_NIX_TEST_NO_SANDBOX") == "") {
            /* This has to appear before import statements. */
            std::string sandboxProfile = "(version 1)\n";

            if (useChroot) {

                /* Lots and lots and lots of file functions freak out if they can't stat their full ancestry */
                PathSet ancestry;

                /* We build the ancestry before adding all inputPaths to the store because we know they'll
                   all have the same parents (the store), and there might be lots of inputs. This isn't
                   particularly efficient... I doubt it'll be a bottleneck in practice */
                for (auto & i : dirsInChroot) {
                    Path cur = i.first;
                    while (cur.compare("/") != 0) {
                        cur = dirOf(cur);
                        ancestry.insert(cur);
                    }
                }

                /* And we want the store in there regardless of how empty dirsInChroot. We include the innermost
                   path component this time, since it's typically /nix/store and we care about that. */
                Path cur = worker.store.storeDir;
                while (cur.compare("/") != 0) {
                    ancestry.insert(cur);
                    cur = dirOf(cur);
                }

                /* Add all our input paths to the chroot */
                for (auto & i : inputPaths)
                    dirsInChroot[i] = i;

                /* Violations will go to the syslog if you set this. Unfortunately the destination does not appear to be configurable */
                if (settings.darwinLogSandboxViolations) {
                    sandboxProfile += "(deny default)\n";
                } else {
                    sandboxProfile += "(deny default (with no-log))\n";
                }

                sandboxProfile += "(import \"sandbox-defaults.sb\")\n";

                if (fixedOutput)
                    sandboxProfile += "(import \"sandbox-network.sb\")\n";

                /* Our rwx outputs */
                sandboxProfile += "(allow file-read* file-write* process-exec\n";
                for (auto & i : missingPaths) {
                    sandboxProfile += (format("\t(subpath \"%1%\")\n") % i.c_str()).str();
                }
                /* Also add redirected outputs to the chroot */
                for (auto & i : redirectedOutputs) {
                    sandboxProfile += (format("\t(subpath \"%1%\")\n") % i.second.c_str()).str();
                }
                sandboxProfile += ")\n";

                /* Our inputs (transitive dependencies and any impurities computed above)

                   without file-write* allowed, access() incorrectly returns EPERM
                 */
                sandboxProfile += "(allow file-read* file-write* process-exec\n";
                for (auto & i : dirsInChroot) {
                    if (i.first != i.second.source)
                        throw Error(format(
                            "can't map '%1%' to '%2%': mismatched impure paths not supported on Darwin")
                            % i.first % i.second.source);

                    string path = i.first;
                    struct stat st;
                    if (lstat(path.c_str(), &st)) {
                        if (i.second.optional && errno == ENOENT)
                            continue;
                        throw SysError(format("getting attributes of path '%1%'") % path);
                    }
                    if (S_ISDIR(st.st_mode))
                        sandboxProfile += (format("\t(subpath \"%1%\")\n") % path).str();
                    else
                        sandboxProfile += (format("\t(literal \"%1%\")\n") % path).str();
                }
                sandboxProfile += ")\n";

                /* Allow file-read* on full directory hierarchy to self. Allows realpath() */
                sandboxProfile += "(allow file-read*\n";
                for (auto & i : ancestry) {
                    sandboxProfile += (format("\t(literal \"%1%\")\n") % i.c_str()).str();
                }
                sandboxProfile += ")\n";

                sandboxProfile += additionalSandboxProfile;
            } else
                sandboxProfile += "(import \"sandbox-minimal.sb\")\n";

            debug("Generated sandbox profile:");
            debug(sandboxProfile);

            Path sandboxFile = tmpDir + "/.sandbox.sb";

            writeFile(sandboxFile, sandboxProfile);

            bool allowLocalNetworking = parsedDrv->getBoolAttr("__darwinAllowLocalNetworking");

            /* The tmpDir in scope points at the temporary build directory for our derivation. Some packages try different mechanisms
               to find temporary directories, so we want to open up a broader place for them to dump their files, if needed. */
            Path globalTmpDir = canonPath(getEnv("TMPDIR", "/tmp"), true);

            /* They don't like trailing slashes on subpath directives */
            if (globalTmpDir.back() == '/') globalTmpDir.pop_back();

            builder = "/usr/bin/sandbox-exec";
            args.push_back("sandbox-exec");
            args.push_back("-f");
            args.push_back(sandboxFile);
            args.push_back("-D");
            args.push_back("_GLOBAL_TMP_DIR=" + globalTmpDir);
            args.push_back("-D");
            args.push_back("IMPORT_DIR=" + settings.nixDataDir + "/nix/sandbox/");
            if (allowLocalNetworking) {
                args.push_back("-D");
                args.push_back(string("_ALLOW_LOCAL_NETWORKING=1"));
            }
            args.push_back(drv->builder);
        }
#endif
        else {
            builder = drv->builder.c_str();
            string builderBasename = baseNameOf(drv->builder);
            args.push_back(builderBasename);
        }

        for (auto & i : drv->args)
            args.push_back(rewriteStrings(i, inputRewrites));

        /* Indicate that we managed to set up the build environment. */
        writeFull(STDERR_FILENO, string("\1\n"));

        /* Execute the program.  This should not return. */
        if (drv->isBuiltin()) {
            try {
                logger = makeJSONLogger(*logger);

                BasicDerivation drv2(*drv);
                for (auto & e : drv2.env)
                    e.second = rewriteStrings(e.second, inputRewrites);

                if (drv->builder == "builtin:fetchurl")
                    builtinFetchurl(drv2, netrcData);
                else if (drv->builder == "builtin:buildenv")
                    builtinBuildenv(drv2);
                else
                    throw Error(format("unsupported builtin function '%1%'") % string(drv->builder, 8));
                _exit(0);
            } catch (std::exception & e) {
                writeFull(STDERR_FILENO, "error: " + string(e.what()) + "\n");
                _exit(1);
            }
        }

        execve(builder, stringsToCharPtrs(args).data(), stringsToCharPtrs(envStrs).data());

        throw SysError(format("executing '%1%'") % drv->builder);

    } catch (std::exception & e) {
        writeFull(STDERR_FILENO, "\1while setting up the build environment: " + string(e.what()) + "\n");
        _exit(1);
    }
}


/* Parse a list of reference specifiers.  Each element must either be
   a store path, or the symbolic name of the output of the derivation
   (such as `out'). */
PathSet parseReferenceSpecifiers(Store & store, const BasicDerivation & drv, const Strings & paths)
{
    PathSet result;
    for (auto & i : paths) {
        if (store.isStorePath(i))
            result.insert(i);
        else if (drv.outputs.find(i) != drv.outputs.end())
            result.insert(drv.outputs.find(i)->second.path);
        else throw BuildError(
            format("derivation contains an illegal reference specifier '%1%'") % i);
    }
    return result;
}


void DerivationGoal::registerOutputs()
{
    /* When using a build hook, the build hook can register the output
       as valid (by doing `nix-store --import').  If so we don't have
       to do anything here. */
    if (hook) {
        bool allValid = true;
        for (auto & i : drv->outputs)
            if (!worker.store.isValidPath(i.second.path)) allValid = false;
        if (allValid) return;
    }

    std::map<std::string, ValidPathInfo> infos;

    /* Set of inodes seen during calls to canonicalisePathMetaData()
       for this build's outputs.  This needs to be shared between
       outputs to allow hard links between outputs. */
    InodesSeen inodesSeen;

    Path checkSuffix = ".check";
    bool keepPreviousRound = settings.keepFailed || settings.runDiffHook;

    std::exception_ptr delayedException;

    /* Check whether the output paths were created, and grep each
       output path to determine what other paths it references.  Also make all
       output paths read-only. */
    for (auto & i : drv->outputs) {
        Path path = i.second.path;
        if (missingPaths.find(path) == missingPaths.end()) continue;

        ValidPathInfo info;

        Path actualPath = path;
        if (useChroot) {
            actualPath = chrootRootDir + path;
            if (pathExists(actualPath)) {
                /* Move output paths from the chroot to the Nix store. */
                if (buildMode == bmRepair)
                    replaceValidPath(path, actualPath);
                else
                    if (buildMode != bmCheck && rename(actualPath.c_str(), worker.store.toRealPath(path).c_str()) == -1)
                        throw SysError(format("moving build output '%1%' from the sandbox to the Nix store") % path);
            }
            if (buildMode != bmCheck) actualPath = worker.store.toRealPath(path);
        }

        if (needsHashRewrite()) {
            Path redirected = redirectedOutputs[path];
            if (buildMode == bmRepair
                && redirectedBadOutputs.find(path) != redirectedBadOutputs.end()
                && pathExists(redirected))
                replaceValidPath(path, redirected);
            if (buildMode == bmCheck && redirected != "")
                actualPath = redirected;
        }

        struct stat st;
        if (lstat(actualPath.c_str(), &st) == -1) {
            if (errno == ENOENT)
                throw BuildError(
                    format("builder for '%1%' failed to produce output path '%2%'")
                    % drvPath % path);
            throw SysError(format("getting attributes of path '%1%'") % actualPath);
        }

#ifndef __CYGWIN__
        /* Check that the output is not group or world writable, as
           that means that someone else can have interfered with the
           build.  Also, the output should be owned by the build
           user. */
        if ((!S_ISLNK(st.st_mode) && (st.st_mode & (S_IWGRP | S_IWOTH))) ||
            (buildUser && st.st_uid != buildUser->getUID()))
            throw BuildError(format("suspicious ownership or permission on '%1%'; rejecting this build output") % path);
#endif

        /* Apply hash rewriting if necessary. */
        bool rewritten = false;
        if (!outputRewrites.empty()) {
            printError(format("warning: rewriting hashes in '%1%'; cross fingers") % path);

            /* Canonicalise first.  This ensures that the path we're
               rewriting doesn't contain a hard link to /etc/shadow or
               something like that. */
            canonicalisePathMetaData(actualPath, buildUser ? buildUser->getUID() : -1, inodesSeen);

            /* FIXME: this is in-memory. */
            StringSink sink;
            dumpPath(actualPath, sink);
            deletePath(actualPath);
            sink.s = make_ref<std::string>(rewriteStrings(*sink.s, outputRewrites));
            StringSource source(*sink.s);
            restorePath(actualPath, source);

            rewritten = true;
        }

        /* Check that fixed-output derivations produced the right
           outputs (i.e., the content hash should match the specified
           hash). */
        if (fixedOutput) {

            bool recursive; Hash h;
            i.second.parseHashInfo(recursive, h);

            if (!recursive) {
                /* The output path should be a regular file without
                   execute permission. */
                if (!S_ISREG(st.st_mode) || (st.st_mode & S_IXUSR) != 0)
                    throw BuildError(
                        format("output path '%1%' should be a non-executable regular file") % path);
            }

            /* Check the hash. In hash mode, move the path produced by
               the derivation to its content-addressed location. */
            Hash h2 = recursive ? hashPath(h.type, actualPath).first : hashFile(h.type, actualPath);

            Path dest = worker.store.makeFixedOutputPath(recursive, h2, storePathToName(path));

            if (h != h2) {

                /* Throw an error after registering the path as
                   valid. */
                worker.hashMismatch = true;
                delayedException = std::make_exception_ptr(
                    BuildError("hash mismatch in fixed-output derivation '%s':\n  wanted: %s\n  got:    %s",
                        dest, h.to_string(), h2.to_string()));

                Path actualDest = worker.store.toRealPath(dest);

                if (worker.store.isValidPath(dest))
                    std::rethrow_exception(delayedException);

                if (actualPath != actualDest) {
                    PathLocks outputLocks({actualDest});
                    deletePath(actualDest);
                    if (rename(actualPath.c_str(), actualDest.c_str()) == -1)
                        throw SysError(format("moving '%1%' to '%2%'") % actualPath % dest);
                }

                path = dest;
                actualPath = actualDest;
            }
            else
                assert(path == dest);

            info.ca = makeFixedOutputCA(recursive, h2);
        }

        /* Get rid of all weird permissions.  This also checks that
           all files are owned by the build user, if applicable. */
        canonicalisePathMetaData(actualPath,
            buildUser && !rewritten ? buildUser->getUID() : -1, inodesSeen);

        /* For this output path, find the references to other paths
           contained in it.  Compute the SHA-256 NAR hash at the same
           time.  The hash is stored in the database so that we can
           verify later on whether nobody has messed with the store. */
        debug("scanning for references inside '%1%'", path);
        HashResult hash;
        PathSet references = scanForReferences(actualPath, allPaths, hash);

        if (buildMode == bmCheck) {
            if (!worker.store.isValidPath(path)) continue;
            auto info = *worker.store.queryPathInfo(path);
            if (hash.first != info.narHash) {
                worker.checkMismatch = true;
                if (settings.runDiffHook || settings.keepFailed) {
                    Path dst = worker.store.toRealPath(path + checkSuffix);
                    deletePath(dst);
                    if (rename(actualPath.c_str(), dst.c_str()))
                        throw SysError(format("renaming '%1%' to '%2%'") % actualPath % dst);

                    handleDiffHook(
                        buildUser ? buildUser->getUID() : getuid(),
                        buildUser ? buildUser->getGID() : getgid(),
                        path, dst, drvPath, tmpDir);

                    throw NotDeterministic(format("derivation '%1%' may not be deterministic: output '%2%' differs from '%3%'")
                        % drvPath % path % dst);
                } else
                    throw NotDeterministic(format("derivation '%1%' may not be deterministic: output '%2%' differs")
                        % drvPath % path);
            }

            /* Since we verified the build, it's now ultimately
               trusted. */
            if (!info.ultimate) {
                info.ultimate = true;
                worker.store.signPathInfo(info);
                worker.store.registerValidPaths({info});
            }

            continue;
        }

        /* For debugging, print out the referenced and unreferenced
           paths. */
        for (auto & i : inputPaths) {
            PathSet::iterator j = references.find(i);
            if (j == references.end())
                debug(format("unreferenced input: '%1%'") % i);
            else
                debug(format("referenced input: '%1%'") % i);
        }

        if (curRound == nrRounds) {
            worker.store.optimisePath(actualPath); // FIXME: combine with scanForReferences()
            worker.markContentsGood(path);
        }

        info.path = path;
        info.narHash = hash.first;
        info.narSize = hash.second;
        info.references = references;
        info.deriver = drvPath;
        info.ultimate = true;
        worker.store.signPathInfo(info);

        if (!info.references.empty()) info.ca.clear();

        infos[i.first] = info;
    }

    if (buildMode == bmCheck) return;

    /* Apply output checks. */
    checkOutputs(infos);

    /* Compare the result with the previous round, and report which
       path is different, if any.*/
    if (curRound > 1 && prevInfos != infos) {
        assert(prevInfos.size() == infos.size());
        for (auto i = prevInfos.begin(), j = infos.begin(); i != prevInfos.end(); ++i, ++j)
            if (!(*i == *j)) {
                result.isNonDeterministic = true;
                Path prev = i->second.path + checkSuffix;
                bool prevExists = keepPreviousRound && pathExists(prev);
                auto msg = prevExists
                    ? fmt("output '%1%' of '%2%' differs from '%3%' from previous round", i->second.path, drvPath, prev)
                    : fmt("output '%1%' of '%2%' differs from previous round", i->second.path, drvPath);

                handleDiffHook(
                    buildUser ? buildUser->getUID() : getuid(),
                    buildUser ? buildUser->getGID() : getgid(),
                    prev, i->second.path, drvPath, tmpDir);

                if (settings.enforceDeterminism)
                    throw NotDeterministic(msg);

                printError(msg);
                curRound = nrRounds; // we know enough, bail out early
            }
    }

    /* If this is the first round of several, then move the output out
       of the way. */
    if (nrRounds > 1 && curRound == 1 && curRound < nrRounds && keepPreviousRound) {
        for (auto & i : drv->outputs) {
            Path prev = i.second.path + checkSuffix;
            deletePath(prev);
            Path dst = i.second.path + checkSuffix;
            if (rename(i.second.path.c_str(), dst.c_str()))
                throw SysError(format("renaming '%1%' to '%2%'") % i.second.path % dst);
        }
    }

    if (curRound < nrRounds) {
        prevInfos = infos;
        return;
    }

    /* Remove the .check directories if we're done. FIXME: keep them
       if the result was not determistic? */
    if (curRound == nrRounds) {
        for (auto & i : drv->outputs) {
            Path prev = i.second.path + checkSuffix;
            deletePath(prev);
        }
    }

    /* Register each output path as valid, and register the sets of
       paths referenced by each of them.  If there are cycles in the
       outputs, this will fail. */
    {
        ValidPathInfos infos2;
        for (auto & i : infos) infos2.push_back(i.second);
        worker.store.registerValidPaths(infos2);
    }

    /* In case of a fixed-output derivation hash mismatch, throw an
       exception now that we have registered the output as valid. */
    if (delayedException)
        std::rethrow_exception(delayedException);
}


void DerivationGoal::checkOutputs(const std::map<Path, ValidPathInfo> & outputs)
{
    std::map<Path, const ValidPathInfo &> outputsByPath;
    for (auto & output : outputs)
        outputsByPath.emplace(output.second.path, output.second);

    for (auto & output : outputs) {
        auto & outputName = output.first;
        auto & info = output.second;

        struct Checks
        {
            bool ignoreSelfRefs = false;
            std::optional<uint64_t> maxSize, maxClosureSize;
            std::optional<Strings> allowedReferences, allowedRequisites, disallowedReferences, disallowedRequisites;
        };

        /* Compute the closure and closure size of some output. This
           is slightly tricky because some of its references (namely
           other outputs) may not be valid yet. */
        auto getClosure = [&](const Path & path)
        {
            uint64_t closureSize = 0;
            PathSet pathsDone;
            std::queue<Path> pathsLeft;
            pathsLeft.push(path);

            while (!pathsLeft.empty()) {
                auto path = pathsLeft.front();
                pathsLeft.pop();
                if (!pathsDone.insert(path).second) continue;

                auto i = outputsByPath.find(path);
                if (i != outputsByPath.end()) {
                    closureSize += i->second.narSize;
                    for (auto & ref : i->second.references)
                        pathsLeft.push(ref);
                } else {
                    auto info = worker.store.queryPathInfo(path);
                    closureSize += info->narSize;
                    for (auto & ref : info->references)
                        pathsLeft.push(ref);
                }
            }

            return std::make_pair(pathsDone, closureSize);
        };

        auto applyChecks = [&](const Checks & checks)
        {
            if (checks.maxSize && info.narSize > *checks.maxSize)
                throw BuildError("path '%s' is too large at %d bytes; limit is %d bytes",
                    info.path, info.narSize, *checks.maxSize);

            if (checks.maxClosureSize) {
                uint64_t closureSize = getClosure(info.path).second;
                if (closureSize > *checks.maxClosureSize)
                    throw BuildError("closure of path '%s' is too large at %d bytes; limit is %d bytes",
                        info.path, closureSize, *checks.maxClosureSize);
            }

            auto checkRefs = [&](const std::optional<Strings> & value, bool allowed, bool recursive)
            {
                if (!value) return;

                PathSet spec = parseReferenceSpecifiers(worker.store, *drv, *value);

                PathSet used = recursive ? getClosure(info.path).first : info.references;

                if (recursive && checks.ignoreSelfRefs)
                    used.erase(info.path);

                PathSet badPaths;

                for (auto & i : used)
                    if (allowed) {
                        if (!spec.count(i))
                            badPaths.insert(i);
                    } else {
                        if (spec.count(i))
                            badPaths.insert(i);
                    }

                if (!badPaths.empty()) {
                    string badPathsStr;
                    for (auto & i : badPaths) {
                        badPathsStr += "\n  ";
                        badPathsStr += i;
                    }
                    throw BuildError("output '%s' is not allowed to refer to the following paths:%s", info.path, badPathsStr);
                }
            };

            checkRefs(checks.allowedReferences, true, false);
            checkRefs(checks.allowedRequisites, true, true);
            checkRefs(checks.disallowedReferences, false, false);
            checkRefs(checks.disallowedRequisites, false, true);
        };

        if (auto structuredAttrs = parsedDrv->getStructuredAttrs()) {
            auto outputChecks = structuredAttrs->find("outputChecks");
            if (outputChecks != structuredAttrs->end()) {
                auto output = outputChecks->find(outputName);

                if (output != outputChecks->end()) {
                    Checks checks;

                    auto maxSize = output->find("maxSize");
                    if (maxSize != output->end())
                        checks.maxSize = maxSize->get<uint64_t>();

                    auto maxClosureSize = output->find("maxClosureSize");
                    if (maxClosureSize != output->end())
                        checks.maxClosureSize = maxClosureSize->get<uint64_t>();

                    auto get = [&](const std::string & name) -> std::optional<Strings> {
                        auto i = output->find(name);
                        if (i != output->end()) {
                            Strings res;
                            for (auto j = i->begin(); j != i->end(); ++j) {
                                if (!j->is_string())
                                    throw Error("attribute '%s' of derivation '%s' must be a list of strings", name, drvPath);
                                res.push_back(j->get<std::string>());
                            }
                            checks.disallowedRequisites = res;
                            return res;
                        }
                        return {};
                    };

                    checks.allowedReferences = get("allowedReferences");
                    checks.allowedRequisites = get("allowedRequisites");
                    checks.disallowedReferences = get("disallowedReferences");
                    checks.disallowedRequisites = get("disallowedRequisites");

                    applyChecks(checks);
                }
            }
        } else {
            // legacy non-structured-attributes case
            Checks checks;
            checks.ignoreSelfRefs = true;
            checks.allowedReferences = parsedDrv->getStringsAttr("allowedReferences");
            checks.allowedRequisites = parsedDrv->getStringsAttr("allowedRequisites");
            checks.disallowedReferences = parsedDrv->getStringsAttr("disallowedReferences");
            checks.disallowedRequisites = parsedDrv->getStringsAttr("disallowedRequisites");
            applyChecks(checks);
        }
    }
}


Path DerivationGoal::openLogFile()
{
    logSize = 0;

    if (!settings.keepLog) return "";

    string baseName = baseNameOf(drvPath);

    /* Create a log file. */
    Path dir = fmt("%s/%s/%s/", worker.store.logDir, worker.store.drvsLogDir, string(baseName, 0, 2));
    createDirs(dir);

    Path logFileName = fmt("%s/%s%s", dir, string(baseName, 2),
        settings.compressLog ? ".bz2" : "");

    fdLogFile = open(logFileName.c_str(), O_CREAT | O_WRONLY | O_TRUNC | O_CLOEXEC, 0666);
    if (!fdLogFile) throw SysError(format("creating log file '%1%'") % logFileName);

    logFileSink = std::make_shared<FdSink>(fdLogFile.get());

    if (settings.compressLog)
        logSink = std::shared_ptr<CompressionSink>(makeCompressionSink("bzip2", *logFileSink));
    else
        logSink = logFileSink;

    return logFileName;
}


void DerivationGoal::closeLogFile()
{
    auto logSink2 = std::dynamic_pointer_cast<CompressionSink>(logSink);
    if (logSink2) logSink2->finish();
    if (logFileSink) logFileSink->flush();
    logSink = logFileSink = 0;
    fdLogFile = -1;
}


void DerivationGoal::deleteTmpDir(bool force)
{
    if (tmpDir != "") {
        /* Don't keep temporary directories for builtins because they
           might have privileged stuff (like a copy of netrc). */
        if (settings.keepFailed && !force && !drv->isBuiltin()) {
            printError(
                format("note: keeping build directory '%2%'")
                % drvPath % tmpDir);
            chmod(tmpDir.c_str(), 0755);
        }
        else
            deletePath(tmpDir);
        tmpDir = "";
    }
}


void DerivationGoal::handleChildOutput(int fd, const string & data)
{
    if ((hook && fd == hook->builderOut.readSide.get()) ||
        (!hook && fd == builderOut.readSide.get()))
    {
        logSize += data.size();
        if (settings.maxLogSize && logSize > settings.maxLogSize) {
            printError(
                format("%1% killed after writing more than %2% bytes of log output")
                % getName() % settings.maxLogSize);
            killChild();
            done(BuildResult::LogLimitExceeded);
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

        if (logSink) (*logSink)(data);
    }

    if (hook && fd == hook->fromHook.readSide.get()) {
        for (auto c : data)
            if (c == '\n') {
                handleJSONLogMessage(currentHookLine, worker.act, hook->activities, true);
                currentHookLine.clear();
            } else
                currentHookLine += c;
    }
}


void DerivationGoal::handleEOF(int fd)
{
    if (!currentLogLine.empty()) flushLine();
    worker.wakeUp(shared_from_this());
}


void DerivationGoal::flushLine()
{
    if (handleJSONLogMessage(currentLogLine, *act, builderActivities, false))
        ;

    else {
        if (settings.verboseBuild &&
            (settings.printRepeatedBuilds || curRound == 1))
            printError(currentLogLine);
        else {
            logTail.push_back(currentLogLine);
            if (logTail.size() > settings.logLines) logTail.pop_front();
        }

        act->result(resBuildLogLine, currentLogLine);
    }

    currentLogLine = "";
    currentLogLinePos = 0;
}


PathSet DerivationGoal::checkPathValidity(bool returnValid, bool checkHash)
{
    PathSet result;
    for (auto & i : drv->outputs) {
        if (!wantOutput(i.first, wantedOutputs)) continue;
        bool good =
            worker.store.isValidPath(i.second.path) &&
            (!checkHash || worker.pathContentsGood(i.second.path));
        if (good == returnValid) result.insert(i.second.path);
    }
    return result;
}


Path DerivationGoal::addHashRewrite(const Path & path)
{
    string h1 = string(path, worker.store.storeDir.size() + 1, 32);
    string h2 = string(hashString(htSHA256, "rewrite:" + drvPath + ":" + path).to_string(Base32, false), 0, 32);
    Path p = worker.store.storeDir + "/" + h2 + string(path, worker.store.storeDir.size() + 33);
    deletePath(p);
    assert(path.size() == p.size());
    inputRewrites[h1] = h2;
    outputRewrites[h2] = h1;
    redirectedOutputs[path] = p;
    return p;
}


void DerivationGoal::done(BuildResult::Status status, const string & msg)
{
    result.status = status;
    result.errorMsg = msg;
    amDone(result.success() ? ecSuccess : ecFailed);
    if (result.status == BuildResult::TimedOut)
        worker.timedOut = true;
    if (result.status == BuildResult::PermanentFailure)
        worker.permanentFailure = true;

    mcExpectedBuilds.reset();
    mcRunningBuilds.reset();

    if (result.success()) {
        if (status == BuildResult::Built)
            worker.doneBuilds++;
    } else {
        if (status != BuildResult::DependencyFailed)
            worker.failedBuilds++;
    }

    worker.updateProgress();
}


}
