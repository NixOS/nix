#include "nix/store/local-store.hh"
#include "nix/store/machines.hh"
#include "nix/store/store-open.hh"
#include "nix/store/build/worker.hh"
#include "nix/store/build/substitution-goal.hh"
#include "nix/store/build/drv-output-substitution-goal.hh"
#include "nix/store/build/derivation-goal.hh"
#include "nix/store/build/derivation-resolution-goal.hh"
#include "nix/store/build/derivation-building-goal.hh"
#include "nix/store/build/derivation-trampoline-goal.hh"
#ifndef _WIN32 // TODO Enable building on Windows
#  include "nix/store/build/hook-instance.hh"
#endif
#include "nix/util/signals.hh"
#include "nix/store/globals.hh"

namespace nix {

Worker::Worker(ref<Store> store, ref<Store> evalStore)
    /* Can't use make_ref, because the constructor is private. */
    : wakerState(ref<Waker>(new Waker{}))
    , act(*logger, actRealise)
    , actDerivations(*logger, actBuilds)
    , actSubstitutions(*logger, actCopyPaths)
#ifdef _WIN32
    , ioport{CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0)}
#endif
    , destStore(std::move(store))
    , srcStore(std::move(evalStore))
    , settings(nix::settings.getWorkerSettings())
    , getSubstituters{[] {
        return nix::settings.getWorkerSettings().useSubstitutes ? getDefaultSubstituters() : std::list<ref<Store>>{};
    }}
{
#ifdef _WIN32
    if (!ioport)
        throw windows::WinError("CreateIoCompletionPort");
    wakerState->ioport = ioport.get();
#endif
    nrLocalBuilds = 0;
    nrSubstitutions = 0;
    lastWokenUp = steady_time_point::min();
}

Worker::~Worker()
{
    /* Explicitly get rid of all strong pointers now.  After this all
       goals that refer to this worker should be gone.  (Otherwise we
       are in trouble, since goals may call childTerminated() etc. in
       their destructors). */
    topGoals.clear();

    assert(expectedSubstitutions == 0);
    assert(expectedDownloadSize == 0);
    assert(expectedNarSize == 0);
}

template<class G, typename... Args>
std::shared_ptr<G> Worker::initGoalIfNeeded(std::weak_ptr<G> & goal_weak, Args &&... args)
{
    if (auto goal = goal_weak.lock())
        return goal;

    auto goal = std::make_shared<G>(args...);
    goal_weak = goal;
    wakeUp(goal);
    return goal;
}

std::shared_ptr<DerivationTrampolineGoal> Worker::makeDerivationTrampolineGoal(
    ref<const SingleDerivedPath> drvReq, const OutputsSpec & wantedOutputs, BuildMode buildMode)
{
    return initGoalIfNeeded(
        derivationTrampolineGoals.ensureSlot(*drvReq).value[wantedOutputs], drvReq, wantedOutputs, *this, buildMode);
}

std::shared_ptr<DerivationTrampolineGoal> Worker::makeDerivationTrampolineGoal(
    const StorePath & drvPath, const OutputsSpec & wantedOutputs, const Derivation & drv, BuildMode buildMode)
{
    return initGoalIfNeeded(
        derivationTrampolineGoals.ensureSlot(DerivedPath::Opaque{drvPath}).value[wantedOutputs],
        drvPath,
        wantedOutputs,
        drv,
        *this,
        buildMode);
}

std::shared_ptr<DerivationGoal> Worker::makeDerivationGoal(
    const StorePath & drvPath,
    ref<const Derivation> drv,
    const OutputName & wantedOutput,
    BuildMode buildMode,
    bool storeDerivation)
{
    return initGoalIfNeeded(
        derivationGoals[drvPath][wantedOutput],
        drvPath,
        std::move(drv),
        wantedOutput,
        *this,
        buildMode,
        storeDerivation);
}

std::shared_ptr<DerivationResolutionGoal>
Worker::makeDerivationResolutionGoal(const StorePath & drvPath, ref<const Derivation> drv, BuildMode buildMode)
{
    return initGoalIfNeeded(derivationResolutionGoals[drvPath], drvPath, drv, *this, buildMode);
}

std::shared_ptr<DerivationBuildingGoal> Worker::makeDerivationBuildingGoal(
    const StorePath & drvPath, ref<const Derivation> drv, BuildMode buildMode, bool storeDerivation)
{
    return initGoalIfNeeded(
        derivationBuildingGoals[drvPath], drvPath, std::move(drv), *this, buildMode, storeDerivation);
}

std::shared_ptr<PathSubstitutionGoal>
Worker::makePathSubstitutionGoal(const StorePath & path, RepairFlag repair, std::optional<ContentAddress> ca)
{
    return initGoalIfNeeded(substitutionGoals[path], path, *this, repair, ca);
}

std::shared_ptr<DrvOutputSubstitutionGoal> Worker::makeDrvOutputSubstitutionGoal(const DrvOutput & id)
{
    return initGoalIfNeeded(drvOutputSubstitutionGoals[id], id, *this);
}

GoalPtr Worker::makeGoal(const DerivedPath & req, BuildMode buildMode)
{
    return std::visit(
        overloaded{
            [&](const DerivedPath::Built & bfd) -> GoalPtr {
                return makeDerivationTrampolineGoal(bfd.drvPath, bfd.outputs, buildMode);
            },
            [&](const DerivedPath::Opaque & bo) -> GoalPtr {
                return makePathSubstitutionGoal(bo.path, buildMode == bmRepair ? Repair : NoRepair);
            },
        },
        req.raw());
}

void Worker::removeGoal(GoalPtr goal)
{
    if (auto drvGoal = std::dynamic_pointer_cast<DerivationTrampolineGoal>(goal)) {
        derivationTrampolineGoals.removeSlot(*drvGoal->drvReq, [&](auto & node) {
            node.value.erase(drvGoal->wantedOutputs);
            /* Return true if ancestors don't need to be pruned. */
            return !node.value.empty();
        });
    } else if (auto drvGoal = std::dynamic_pointer_cast<DerivationGoal>(goal)) {
        if (auto it = derivationGoals.find(drvGoal->drvPath); it != derivationGoals.end()) {
            it->second.erase(drvGoal->wantedOutput);
            if (it->second.empty())
                derivationGoals.erase(it);
        }
    } else if (auto drvResolutionGoal = std::dynamic_pointer_cast<DerivationResolutionGoal>(goal)) {
        derivationResolutionGoals.erase(drvResolutionGoal->drvPath);
    } else if (auto drvBuildingGoal = std::dynamic_pointer_cast<DerivationBuildingGoal>(goal)) {
        derivationBuildingGoals.erase(drvBuildingGoal->drvPath);
    } else if (auto subGoal = std::dynamic_pointer_cast<PathSubstitutionGoal>(goal)) {
        substitutionGoals.erase(subGoal->storePath);
    } else if (auto subGoal = std::dynamic_pointer_cast<DrvOutputSubstitutionGoal>(goal)) {
        drvOutputSubstitutionGoals.erase(subGoal->id);
    } else {
        unreachable();
    }

    if (topGoals.find(goal) != topGoals.end()) {
        topGoals.erase(goal);
        /* If a top-level goal failed, then kill all other goals
           (unless keepGoing was set). */
        if (goal->exitCode == Goal::ecFailed && !settings.keepGoing)
            topGoals.clear();
    }
}

void Worker::wakeUp(GoalPtr goal)
{
    goal->trace("woken up");
    addToWeakGoals(awake, goal);
}

size_t Worker::getNrLocalBuilds()
{
    return nrLocalBuilds;
}

size_t Worker::getNrSubstitutions()
{
    return nrSubstitutions;
}

void Worker::childStarted(
    GoalPtr goal, const std::set<MuxablePipePollState::CommChannel> & channels, bool inBuildSlot, bool respectTimeouts)
{
    Child child;
    child.goal = goal;
    child.goal2 = goal.get();
    child.channels = channels;
    child.timeStarted = child.lastOutput = steady_time_point::clock::now();
    child.inBuildSlot = inBuildSlot;
    child.respectTimeouts = respectTimeouts;
    children.emplace_back(child);
    if (inBuildSlot) {
        switch (goal->jobCategory()) {
        case JobCategory::Substitution:
            nrSubstitutions++;
            break;
        case JobCategory::Build:
            nrLocalBuilds++;
            break;
        case JobCategory::Administration:
        default:
            /* Doesn't make sense, since there are only building and substitution slots. */
            unreachable();
        }
    }
}

void Worker::childTerminated(Goal * goal)
{
    childTerminated(goal, goal->jobCategory());
}

void Worker::childTerminated(Goal * goal, JobCategory jobCategory)
{
    // FIXME: Inefficient. Make children a map from Goal -> Child instead.
    auto i = std::find_if(children.begin(), children.end(), [&](const Child & child) { return child.goal2 == goal; });
    if (i == children.end())
        return;

    if (i->inBuildSlot) {
        switch (jobCategory) {
        case JobCategory::Substitution:
            assert(nrSubstitutions > 0);
            nrSubstitutions--;
            break;
        case JobCategory::Build:
            assert(nrLocalBuilds > 0);
            nrLocalBuilds--;
            break;
        case JobCategory::Administration:
        default:
            /* Doesn't make sense, since there are only building and substitution slots. */
            unreachable();
        }
    }

    children.erase(i);
    auto & waiting = jobCategory == JobCategory::Substitution ? wantingToSubstitute : wantingToBuild;

    /* Wake up goals waiting for a build slot. Wake at most one waiter to avoid
       starting unnecessary work (that is accompanied by coroutine frame allocation). */
    auto it = waiting.begin();
    while (it != waiting.end()) {
        if (auto goal = it->lock()) {
            waiting.erase(it);
            wakeUp(goal);
            break;
        }
        it = waiting.erase(it);
    }
}

void Worker::waitForBuildSlot(GoalPtr goal)
{
    goal->trace("wait for build slot");

    bool slotAvailable = [&] {
        if (goal->jobCategory() == JobCategory::Substitution)
            return getNrSubstitutions() < settings.maxSubstitutionJobs;
        else
            return getNrLocalBuilds() < settings.maxBuildJobs;
    }();

    if (slotAvailable)
        wakeUp(goal); /* Can do it right away. */
    else
        addToWeakGoals(goal->jobCategory() == JobCategory::Substitution ? wantingToSubstitute : wantingToBuild, goal);
}

void Worker::waitForAWhile(GoalPtr goal)
{
    goal->trace("wait for a while");
    addToWeakGoals(waitingForAWhile, goal);
}

void Worker::waitForCompletion(GoalPtr goal)
{
    goal->trace("waiting for completion callback");
    addToWeakGoals(waitingForCompletion, goal);
}

void Worker::run(const Goals & _topGoals)
{
    std::vector<nix::DerivedPath> topPaths;

    for (auto & i : _topGoals) {
        topGoals.insert(i);
        if (auto goal = dynamic_cast<DerivationTrampolineGoal *>(i.get())) {
            topPaths.push_back(
                DerivedPath::Built{
                    .drvPath = goal->drvReq,
                    .outputs = goal->wantedOutputs,
                });
        } else if (auto goal = dynamic_cast<PathSubstitutionGoal *>(i.get())) {
            topPaths.push_back(DerivedPath::Opaque{goal->storePath});
        }
    }

    /* Call queryMissing() to efficiently query substitutes. */
    store.queryMissing(topPaths);

    debug("entered goal loop");

    while (1) {

        checkInterrupt();

        // TODO GC interface?
        if (auto localStore = dynamic_cast<LocalStore *>(&store))
            localStore->autoGC(false);

        /* Call every wake goal (in the ordering established by
           CompareGoalPtrs). */
        while (!awake.empty() && !topGoals.empty()) {
            Goals awake2;
            for (auto & i : awake) {
                GoalPtr goal = i.lock();
                if (goal)
                    awake2.insert(goal);
            }
            awake.clear();

            for (auto & goal : awake2) {
                checkInterrupt();

                std::chrono::time_point<std::chrono::steady_clock> startTime;
                if (verbosity >= lvlVomit)
                    startTime = std::chrono::steady_clock::now();

                goal->work();

                /* Useful for tracing which goals hod the event loop. */
                vomit(
                    "worker event loop worked goal '%1%' for %2$.3fms",
                    goal->name,
                    std::chrono::duration_cast<std::chrono::duration<float, std::milli>>(
                        std::chrono::steady_clock::now() - startTime)
                        .count());

                if (topGoals.empty())
                    break; // stuff may have been cancelled
            }
        }

        if (topGoals.empty())
            break;

        /* Wait for input or completion callbacks. */
        if (!children.empty() || !waitingForAWhile.empty() || !waitingForCompletion.empty())
            waitForInput();
        else if (awake.empty() && 0U == settings.maxBuildJobs) {
            if (Machine::parseConfig({nix::settings.thisSystem}, nix::settings.getWorkerSettings().builders).empty())
                throw Error(
                    "Unable to start any build; either increase '--max-jobs' or enable remote builds.\n"
                    "\n"
                    "For more information run 'man nix.conf' and search for '/machines'.");
            else
                throw Error(
                    "Unable to start any build; remote machines may not have all required system features.\n"
                    "\n"
                    "For more information run 'man nix.conf' and search for '/machines'.");
        } else
            assert(!awake.empty());
    }

    /* If --keep-going is not set, it's possible that the main goal
       exited while some of its subgoals were still active.  But if
       --keep-going *is* set, then they must all be finished now. */
    assert(!settings.keepGoing || awake.empty());
    assert(!settings.keepGoing || wantingToBuild.empty());
    assert(!settings.keepGoing || children.empty());
}

void Worker::waitForInput()
{
    printMsg(lvlVomit, "waiting for children");

    /* Process output from the file descriptors attached to the
       children, namely log output and output path creation commands.
       We also use this to detect child termination: if we get EOF on
       the logger pipe of a build, we assume that the builder has
       terminated. */

    bool useTimeout = false;
    long timeout = 0;
    auto before = steady_time_point::clock::now();

    /* If we're monitoring for silence on stdout/stderr, or if there
       is a build timeout, then wait for input until the first
       deadline for any child. */
    auto nearest = steady_time_point::max(); // nearest deadline

    auto localStore = dynamic_cast<LocalStore *>(&store);
    if (localStore && localStore->config->getLocalSettings().getGCSettings().minFree.get() != 0)
        // If we have a local store (and thus are capable of automatically collecting garbage) and configured to do so,
        // periodically wake up to see if we need to run the garbage collector. (See the `autoGC` call site above in
        // this file, also gated on having a local store. when we wake up, we intended to reach that call site.)
        nearest = before + std::chrono::seconds(10);
    for (auto & i : children) {
        if (!i.respectTimeouts)
            continue;
        if (0 != settings.maxSilentTime)
            nearest = std::min(nearest, i.lastOutput + std::chrono::seconds(settings.maxSilentTime));
        if (0 != settings.buildTimeout)
            nearest = std::min(nearest, i.timeStarted + std::chrono::seconds(settings.buildTimeout));
    }
    if (nearest != steady_time_point::max()) {
        timeout = std::max(1L, (long) std::chrono::duration_cast<std::chrono::seconds>(nearest - before).count());
        useTimeout = true;
    }

    /* If we are polling goals that are waiting for a lock, then wake
       up after a few seconds at most. */
    if (!waitingForAWhile.empty()) {
        useTimeout = true;
        if (lastWokenUp == steady_time_point::min() || lastWokenUp > before)
            lastWokenUp = before;
        timeout = std::max(
            1L,
            (long) std::chrono::duration_cast<std::chrono::seconds>(
                lastWokenUp + std::chrono::seconds(settings.pollInterval) - before)
                .count());
    } else
        lastWokenUp = steady_time_point::min();

    if (useTimeout)
        vomit("sleeping %d seconds", timeout);

    MuxablePipePollState state;

#ifndef _WIN32
    /* Use select() to wait for the input side of any logger pipe to
       become `available'.  Note that `available' (i.e., non-blocking)
       includes EOF. */
    for (auto & i : children) {
        for (auto & j : i.channels) {
            state.pollStatus.push_back((struct pollfd) {.fd = j, .events = POLLIN});
            state.fdToPollStatus[j] = state.pollStatus.size() - 1;
        }
    }

    {
        auto wakeupPipeFd = wakerState->wakeupPipe.pipe.readSide.get();
        state.pollStatus.push_back(
            pollfd{
                .fd = wakeupPipeFd,
                .events = POLLIN,
            });

        state.fdToPollStatus[wakeupPipeFd] = state.pollStatus.size() - 1;
    }
#endif

    state.poll(
#ifdef _WIN32
        ioport.get(),
#endif
        useTimeout ? (std::optional{timeout * 1000}) : std::nullopt);

    auto after = steady_time_point::clock::now();

    /* Process all available file descriptors. FIXME: this is
       O(children * fds). */
    decltype(children)::iterator i;
    for (auto j = children.begin(); j != children.end(); j = i) {
        i = std::next(j);

        checkInterrupt();

        GoalPtr goal = j->goal.lock();
        assert(goal);

        state.iterate(
            j->channels,
            [&](Descriptor k, std::string_view data) {
                printMsg(lvlVomit, "%1%: read %2% bytes", goal->getName(), data.size());
                j->lastOutput = after;
                goal->handleChildOutput(k, data);
            },
            [&](Descriptor k) {
                debug("%1%: got EOF", goal->getName());
                goal->handleEOF(k);
            });

        if (goal->exitCode == Goal::ecBusy && 0 != settings.maxSilentTime && j->respectTimeouts
            && after - j->lastOutput >= std::chrono::seconds(settings.maxSilentTime)) {
            goal->timedOut(TimedOut(settings.maxSilentTime));
        }

        else if (
            goal->exitCode == Goal::ecBusy && 0 != settings.buildTimeout && j->respectTimeouts
            && after - j->timeStarted >= std::chrono::seconds(settings.buildTimeout)) {
            goal->timedOut(TimedOut(settings.buildTimeout));
        }
    }

#ifndef _WIN32
    std::set<MuxablePipePollState::CommChannel> wakerChannels{wakerState->wakeupPipe.pipe.readSide.get()};
    state.iterate(
        wakerChannels,
        [&](Descriptor k, std::string_view data) { wakerState->wakeAll(*this); },
        [](Descriptor fd) { unreachable(); });
#else
    /* Slightly less optimal on windows. We don't use a wakeup pipe and signal the ioport directly. */
    wakerState->wakeAll(*this);
#endif

    if (!waitingForAWhile.empty() && lastWokenUp + std::chrono::seconds(settings.pollInterval) <= after) {
        lastWokenUp = after;
        for (auto & i : waitingForAWhile) {
            GoalPtr goal = i.lock();
            if (goal)
                wakeUp(goal);
        }
        waitingForAWhile.clear();
    }
}

std::weak_ptr<Worker::Waker> Worker::getCrossThreadWaker()
{
    return wakerState.get_ptr();
}

void Worker::Waker::wakeAll(Worker & worker)
{
    /* Wake up all goals that have been enqueued by asynchronous completion callbacks. */
    auto wakeupQueue(wakeupQueue_.lock());
#ifndef _WIN32
    wakeupPipe.drain();
#endif
    while (!wakeupQueue->empty()) {
        auto ptr = wakeupQueue->front().lock();
        wakeupQueue->pop();
        if (ptr) {
            worker.waitingForCompletion.erase(ptr);
            worker.wakeUp(ptr);
        }
    }
}

void Worker::Waker::enqueue(WeakGoalPtr goal)
{
    wakeupQueue_.lock()->push(goal);
#ifdef _WIN32
    PostQueuedCompletionStatus(
        ioport, /*dwNumberOfBytesTransferred=*/0, /*dwCompletionKey=*/0, /*lpOverlapped=*/nullptr);
#else
    wakeupPipe.notify();
#endif
}

bool Worker::pathContentsGood(const StorePath & path)
{
    auto i = pathContentsGoodCache.find(path);
    if (i != pathContentsGoodCache.end())
        return i->second;
    printInfo("checking path '%s'...", store.printStorePath(path));
    auto info = store.queryPathInfo(path);
    bool res = false;
    if (auto accessor = store.getFSAccessor(path, /*requireValidPath=*/false)) {
        auto current = hashPath({ref{accessor}}, FileIngestionMethod::NixArchive, info->narHash.algo).first;
        Hash nullHash(HashAlgorithm::SHA256);
        res = info->narHash == nullHash || info->narHash == current;
    }
    pathContentsGoodCache.insert_or_assign(path, res);
    if (!res)
        printError("path '%s' is corrupted or missing!", store.printStorePath(path));
    return res;
}

void Worker::markContentsGood(const StorePath & path)
{
    pathContentsGoodCache.insert_or_assign(path, true);
}

GoalPtr upcast_goal(std::shared_ptr<PathSubstitutionGoal> subGoal)
{
    return subGoal;
}

GoalPtr upcast_goal(std::shared_ptr<DrvOutputSubstitutionGoal> subGoal)
{
    return subGoal;
}

GoalPtr upcast_goal(std::shared_ptr<DerivationGoal> subGoal)
{
    return subGoal;
}

} // namespace nix
