#include "nix/store/local-store.hh"
#include "nix/store/machines.hh"
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

Worker::Worker(Store & store, Store & evalStore)
    : act(*logger, actRealise)
    , actDerivations(*logger, actBuilds)
    , actSubstitutions(*logger, actCopyPaths)
    , store(store)
    , evalStore(evalStore)
{
    nrLocalBuilds = 0;
    nrSubstitutions = 0;
    lastWokenUp = steady_time_point::min();
    permanentFailure = false;
    timedOut = false;
    hashMismatch = false;
    checkMismatch = false;
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
    const Derivation & drv,
    const OutputName & wantedOutput,
    BuildMode buildMode,
    bool storeDerivation)
{
    return initGoalIfNeeded(
        derivationGoals[drvPath][wantedOutput], drvPath, drv, wantedOutput, *this, buildMode, storeDerivation);
}

std::shared_ptr<DerivationResolutionGoal>
Worker::makeDerivationResolutionGoal(const StorePath & drvPath, const Derivation & drv, BuildMode buildMode)
{
    return initGoalIfNeeded(derivationResolutionGoals[drvPath], drvPath, drv, *this, buildMode);
}

std::shared_ptr<DerivationBuildingGoal> Worker::makeDerivationBuildingGoal(
    const StorePath & drvPath, const Derivation & drv, BuildMode buildMode, bool storeDerivation)
{
    return initGoalIfNeeded(derivationBuildingGoals[drvPath], drvPath, drv, *this, buildMode, storeDerivation);
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

/**
 * This function is polymorphic (both via type parameters and
 * overloading) and recursive in order to work on a various types of
 * trees
 *
 * @return Whether the tree node we are processing is not empty / should
 * be kept alive. In the case of this overloading the node in question
 * is the leaf, the weak reference itself. If the weak reference points
 * to the goal we are looking for, our caller can delete it. In the
 * inductive case where the node is an interior node, we'll likewise
 * return whether the interior node is non-empty. If it is empty
 * (because we just deleted its last child), then our caller can
 * likewise delete it.
 */
template<typename G>
static bool removeGoal(std::shared_ptr<G> goal, std::weak_ptr<G> & gp)
{
    return gp.lock() != goal;
}

template<typename K, typename G, typename Inner>
static bool removeGoal(std::shared_ptr<G> goal, std::map<K, Inner> & goalMap)
{
    /* !!! inefficient */
    for (auto i = goalMap.begin(); i != goalMap.end();) {
        if (!removeGoal(goal, i->second))
            i = goalMap.erase(i);
        else
            ++i;
    }
    return !goalMap.empty();
}

template<typename G>
static bool
removeGoal(std::shared_ptr<G> goal, typename DerivedPathMap<std::map<OutputsSpec, std::weak_ptr<G>>>::ChildNode & node)
{
    return removeGoal(goal, node.value) || removeGoal(goal, node.childMap);
}

void Worker::removeGoal(GoalPtr goal)
{
    if (auto drvGoal = std::dynamic_pointer_cast<DerivationTrampolineGoal>(goal))
        nix::removeGoal(drvGoal, derivationTrampolineGoals.map);
    else if (auto drvGoal = std::dynamic_pointer_cast<DerivationGoal>(goal))
        nix::removeGoal(drvGoal, derivationGoals);
    else if (auto drvResolutionGoal = std::dynamic_pointer_cast<DerivationResolutionGoal>(goal))
        nix::removeGoal(drvResolutionGoal, derivationResolutionGoals);
    else if (auto drvBuildingGoal = std::dynamic_pointer_cast<DerivationBuildingGoal>(goal))
        nix::removeGoal(drvBuildingGoal, derivationBuildingGoals);
    else if (auto subGoal = std::dynamic_pointer_cast<PathSubstitutionGoal>(goal))
        nix::removeGoal(subGoal, substitutionGoals);
    else if (auto subGoal = std::dynamic_pointer_cast<DrvOutputSubstitutionGoal>(goal))
        nix::removeGoal(subGoal, drvOutputSubstitutionGoals);
    else
        assert(false);

    if (topGoals.find(goal) != topGoals.end()) {
        topGoals.erase(goal);
        /* If a top-level goal failed, then kill all other goals
           (unless keepGoing was set). */
        if (goal->exitCode == Goal::ecFailed && !settings.keepGoing)
            topGoals.clear();
    }

    /* Wake up goals waiting for any goal to finish. */
    for (auto & i : waitingForAnyGoal) {
        GoalPtr goal = i.lock();
        if (goal)
            wakeUp(goal);
    }

    waitingForAnyGoal.clear();
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
            /* Intentionally not limited, see docs */
            break;
        default:
            unreachable();
        }
    }
}

void Worker::childTerminated(Goal * goal, bool wakeSleepers)
{
    auto i = std::find_if(children.begin(), children.end(), [&](const Child & child) { return child.goal2 == goal; });
    if (i == children.end())
        return;

    if (i->inBuildSlot) {
        switch (goal->jobCategory()) {
        case JobCategory::Substitution:
            assert(nrSubstitutions > 0);
            nrSubstitutions--;
            break;
        case JobCategory::Build:
            assert(nrLocalBuilds > 0);
            nrLocalBuilds--;
            break;
        case JobCategory::Administration:
            /* Intentionally not limited, see docs */
            break;
        default:
            unreachable();
        }
    }

    children.erase(i);

    if (wakeSleepers) {

        /* Wake up goals waiting for a build slot. */
        for (auto & j : wantingToBuild) {
            GoalPtr goal = j.lock();
            if (goal)
                wakeUp(goal);
        }

        wantingToBuild.clear();
    }
}

void Worker::waitForBuildSlot(GoalPtr goal)
{
    goal->trace("wait for build slot");
    bool isSubstitutionGoal = goal->jobCategory() == JobCategory::Substitution;
    if ((!isSubstitutionGoal && getNrLocalBuilds() < settings.maxBuildJobs)
        || (isSubstitutionGoal && getNrSubstitutions() < settings.maxSubstitutionJobs))
        wakeUp(goal); /* we can do it right away */
    else
        addToWeakGoals(wantingToBuild, goal);
}

void Worker::waitForAnyGoal(GoalPtr goal)
{
    debug("wait for any goal");
    addToWeakGoals(waitingForAnyGoal, goal);
}

void Worker::waitForAWhile(GoalPtr goal)
{
    debug("wait for a while");
    addToWeakGoals(waitingForAWhile, goal);
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
                goal->work();
                if (topGoals.empty())
                    break; // stuff may have been cancelled
            }
        }

        if (topGoals.empty())
            break;

        /* Wait for input. */
        if (!children.empty() || !waitingForAWhile.empty())
            waitForInput();
        else if (awake.empty() && 0U == settings.maxBuildJobs) {
            if (getMachines().empty())
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
    if (settings.minFree.get() != 0)
        // Periodicallty wake up to see if we need to run the garbage collector.
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
            goal->timedOut(
                Error("%1% timed out after %2% seconds of silence", goal->getName(), settings.maxSilentTime));
        }

        else if (
            goal->exitCode == Goal::ecBusy && 0 != settings.buildTimeout && j->respectTimeouts
            && after - j->timeStarted >= std::chrono::seconds(settings.buildTimeout)) {
            goal->timedOut(Error("%1% timed out after %2% seconds", goal->getName(), settings.buildTimeout));
        }
    }

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

unsigned int Worker::failingExitStatus()
{
    // See API docs in header for explanation
    unsigned int mask = 0;
    bool buildFailure = permanentFailure || timedOut || hashMismatch;
    if (buildFailure)
        mask |= 0x04; // 100
    if (timedOut)
        mask |= 0x01; // 101
    if (hashMismatch)
        mask |= 0x02; // 102
    if (checkMismatch) {
        mask |= 0x08; // 104
    }

    if (mask)
        mask |= 0x60;
    return mask ? mask : 1;
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
