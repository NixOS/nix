#include "machines.hh"
#include "worker.hh"
#include "substitution-goal.hh"
#include "drv-output-substitution-goal.hh"
#include "create-derivation-and-realise-goal.hh"
#include "local-derivation-goal.hh"
#include "hook-instance.hh"

#include <poll.h>

namespace nix {

Worker::Worker(Store & store, Store & evalStore)
    : act(*logger, actRealise)
    , actDerivations(*logger, actBuilds)
    , actSubstitutions(*logger, actCopyPaths)
    , store(store)
    , evalStore(evalStore)
{
    /* Debugging: prevent recursive workers. */
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


std::shared_ptr<CreateDerivationAndRealiseGoal> Worker::makeCreateDerivationAndRealiseGoal(
    ref<SingleDerivedPath> drvReq,
    const OutputsSpec & wantedOutputs,
    BuildMode buildMode)
{
    std::weak_ptr<CreateDerivationAndRealiseGoal> & goal_weak = outerDerivationGoals.ensureSlot(*drvReq).value;
    std::shared_ptr<CreateDerivationAndRealiseGoal> goal = goal_weak.lock();
    if (!goal) {
        goal = std::make_shared<CreateDerivationAndRealiseGoal>(drvReq, wantedOutputs, *this, buildMode);
        goal_weak = goal;
        wakeUp(goal);
    } else {
        goal->addWantedOutputs(wantedOutputs);
    }
    return goal;
}


std::shared_ptr<DerivationGoal> Worker::makeDerivationGoalCommon(
    const StorePath & drvPath,
    const OutputsSpec & wantedOutputs,
    std::function<std::shared_ptr<DerivationGoal>()> mkDrvGoal)
{
    std::weak_ptr<DerivationGoal> & goal_weak = derivationGoals[drvPath];
    std::shared_ptr<DerivationGoal> goal = goal_weak.lock();
    if (!goal) {
        goal = mkDrvGoal();
        goal_weak = goal;
        wakeUp(goal);
    } else {
        goal->addWantedOutputs(wantedOutputs);
    }
    return goal;
}


std::shared_ptr<DerivationGoal> Worker::makeDerivationGoal(const StorePath & drvPath,
    const OutputsSpec & wantedOutputs, BuildMode buildMode)
{
    return makeDerivationGoalCommon(drvPath, wantedOutputs, [&]() -> std::shared_ptr<DerivationGoal> {
        return !dynamic_cast<LocalStore *>(&store)
            ? std::make_shared</* */DerivationGoal>(drvPath, wantedOutputs, *this, buildMode)
            : std::make_shared<LocalDerivationGoal>(drvPath, wantedOutputs, *this, buildMode);
    });
}


std::shared_ptr<DerivationGoal> Worker::makeBasicDerivationGoal(const StorePath & drvPath,
    const BasicDerivation & drv, const OutputsSpec & wantedOutputs, BuildMode buildMode)
{
    return makeDerivationGoalCommon(drvPath, wantedOutputs, [&]() -> std::shared_ptr<DerivationGoal> {
        return !dynamic_cast<LocalStore *>(&store)
            ? std::make_shared</* */DerivationGoal>(drvPath, drv, wantedOutputs, *this, buildMode)
            : std::make_shared<LocalDerivationGoal>(drvPath, drv, wantedOutputs, *this, buildMode);
    });
}


std::shared_ptr<PathSubstitutionGoal> Worker::makePathSubstitutionGoal(const StorePath & path, RepairFlag repair, std::optional<ContentAddress> ca)
{
    std::weak_ptr<PathSubstitutionGoal> & goal_weak = substitutionGoals[path];
    auto goal = goal_weak.lock(); // FIXME
    if (!goal) {
        goal = std::make_shared<PathSubstitutionGoal>(path, *this, repair, ca);
        goal_weak = goal;
        wakeUp(goal);
    }
    return goal;
}


std::shared_ptr<DrvOutputSubstitutionGoal> Worker::makeDrvOutputSubstitutionGoal(const DrvOutput& id, RepairFlag repair, std::optional<ContentAddress> ca)
{
    std::weak_ptr<DrvOutputSubstitutionGoal> & goal_weak = drvOutputSubstitutionGoals[id];
    auto goal = goal_weak.lock(); // FIXME
    if (!goal) {
        goal = std::make_shared<DrvOutputSubstitutionGoal>(id, *this, repair, ca);
        goal_weak = goal;
        wakeUp(goal);
    }
    return goal;
}


GoalPtr Worker::makeGoal(const DerivedPath & req, BuildMode buildMode)
{
    return std::visit(overloaded {
        [&](const DerivedPath::Built & bfd) -> GoalPtr {
            return makeCreateDerivationAndRealiseGoal(bfd.drvPath, bfd.outputs, buildMode);
        },
        [&](const DerivedPath::Opaque & bo) -> GoalPtr {
            return makePathSubstitutionGoal(bo.path, buildMode == bmRepair ? Repair : NoRepair);
        },
    }, req.raw());
}


template<typename K, typename V, typename F>
static void cullMap(std::map<K, V> & goalMap, F f)
{
    for (auto i = goalMap.begin(); i != goalMap.end();)
        if (!f(i->second))
            i = goalMap.erase(i);
        else ++i;
}


template<typename K, typename G>
static void removeGoal(std::shared_ptr<G> goal, std::map<K, std::weak_ptr<G>> & goalMap)
{
    /* !!! inefficient */
    cullMap(goalMap, [&](const std::weak_ptr<G> & gp) -> bool {
        return gp.lock() != goal;
    });
}

template<typename K>
static void removeGoal(std::shared_ptr<CreateDerivationAndRealiseGoal> goal, std::map<K, DerivedPathMap<std::weak_ptr<CreateDerivationAndRealiseGoal>>::ChildNode> & goalMap);

template<typename K>
static void removeGoal(std::shared_ptr<CreateDerivationAndRealiseGoal> goal, std::map<K, DerivedPathMap<std::weak_ptr<CreateDerivationAndRealiseGoal>>::ChildNode> & goalMap)
{
    /* !!! inefficient */
    cullMap(goalMap, [&](DerivedPathMap<std::weak_ptr<CreateDerivationAndRealiseGoal>>::ChildNode & node) -> bool {
        if (node.value.lock() == goal)
            node.value.reset();
        removeGoal(goal, node.childMap);
        return !node.value.expired() || !node.childMap.empty();
    });
}


void Worker::removeGoal(GoalPtr goal)
{
    if (auto drvGoal = std::dynamic_pointer_cast<CreateDerivationAndRealiseGoal>(goal))
        nix::removeGoal(drvGoal, outerDerivationGoals.map);
    else if (auto drvGoal = std::dynamic_pointer_cast<DerivationGoal>(goal))
        nix::removeGoal(drvGoal, derivationGoals);
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
        if (goal) wakeUp(goal);
    }

    waitingForAnyGoal.clear();
}


void Worker::wakeUp(GoalPtr goal)
{
    goal->trace("woken up");
    addToWeakGoals(awake, goal);
}


unsigned Worker::getNrLocalBuilds()
{
    return nrLocalBuilds;
}


unsigned Worker::getNrSubstitutions()
{
    return nrSubstitutions;
}


void Worker::childStarted(GoalPtr goal, const std::set<int> & fds,
    bool inBuildSlot, bool respectTimeouts)
{
    Child child;
    child.goal = goal;
    child.goal2 = goal.get();
    child.fds = fds;
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
            abort();
        }
    }
}


void Worker::childTerminated(Goal * goal, bool wakeSleepers)
{
    auto i = std::find_if(children.begin(), children.end(),
        [&](const Child & child) { return child.goal2 == goal; });
    if (i == children.end()) return;

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
            abort();
        }
    }

    children.erase(i);

    if (wakeSleepers) {

        /* Wake up goals waiting for a build slot. */
        for (auto & j : wantingToBuild) {
            GoalPtr goal = j.lock();
            if (goal) wakeUp(goal);
        }

        wantingToBuild.clear();
    }
}


void Worker::waitForBuildSlot(GoalPtr goal)
{
    debug("wait for build slot");
    bool isSubstitutionGoal = goal->jobCategory() == JobCategory::Substitution;
    if ((!isSubstitutionGoal && getNrLocalBuilds() < settings.maxBuildJobs) ||
        (isSubstitutionGoal && getNrSubstitutions() < settings.maxSubstitutionJobs))
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
        if (auto goal = dynamic_cast<CreateDerivationAndRealiseGoal *>(i.get())) {
            topPaths.push_back(DerivedPath::Built {
                .drvPath = goal->drvReq,
                .outputs = goal->wantedOutputs,
            });
        } else if (auto goal = dynamic_cast<PathSubstitutionGoal *>(i.get())) {
            topPaths.push_back(DerivedPath::Opaque{goal->storePath});
        }
    }

    /* Call queryMissing() to efficiently query substitutes. */
    StorePathSet willBuild, willSubstitute, unknown;
    uint64_t downloadSize, narSize;
    store.queryMissing(topPaths, willBuild, willSubstitute, unknown, downloadSize, narSize);

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
                if (goal) awake2.insert(goal);
            }
            awake.clear();
            for (auto & goal : awake2) {
                checkInterrupt();
                goal->work();
                if (topGoals.empty()) break; // stuff may have been cancelled
            }
        }

        if (topGoals.empty()) break;

        /* Wait for input. */
        if (!children.empty() || !waitingForAWhile.empty())
            waitForInput();
        else {
            if (awake.empty() && 0U == settings.maxBuildJobs)
            {
                if (getMachines().empty())
                   throw Error("unable to start any build; either increase '--max-jobs' "
                            "or enable remote builds."
                            "\nhttps://nixos.org/manual/nix/stable/advanced-topics/distributed-builds.html");
                else
                   throw Error("unable to start any build; remote machines may not have "
                            "all required system features."
                            "\nhttps://nixos.org/manual/nix/stable/advanced-topics/distributed-builds.html");

            }
            assert(!awake.empty());
        }
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
        if (!i.respectTimeouts) continue;
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
        if (lastWokenUp == steady_time_point::min() || lastWokenUp > before) lastWokenUp = before;
        timeout = std::max(1L,
            (long) std::chrono::duration_cast<std::chrono::seconds>(
                lastWokenUp + std::chrono::seconds(settings.pollInterval) - before).count());
    } else lastWokenUp = steady_time_point::min();

    if (useTimeout)
        vomit("sleeping %d seconds", timeout);

    /* Use select() to wait for the input side of any logger pipe to
       become `available'.  Note that `available' (i.e., non-blocking)
       includes EOF. */
    std::vector<struct pollfd> pollStatus;
    std::map<int, size_t> fdToPollStatus;
    for (auto & i : children) {
        for (auto & j : i.fds) {
            pollStatus.push_back((struct pollfd) { .fd = j, .events = POLLIN });
            fdToPollStatus[j] = pollStatus.size() - 1;
        }
    }

    if (poll(pollStatus.data(), pollStatus.size(),
            useTimeout ? timeout * 1000 : -1) == -1) {
        if (errno == EINTR) return;
        throw SysError("waiting for input");
    }

    auto after = steady_time_point::clock::now();

    /* Process all available file descriptors. FIXME: this is
       O(children * fds). */
    decltype(children)::iterator i;
    for (auto j = children.begin(); j != children.end(); j = i) {
        i = std::next(j);

        checkInterrupt();

        GoalPtr goal = j->goal.lock();
        assert(goal);

        std::set<int> fds2(j->fds);
        std::vector<unsigned char> buffer(4096);
        for (auto & k : fds2) {
            const auto fdPollStatusId = get(fdToPollStatus, k);
            assert(fdPollStatusId);
            assert(*fdPollStatusId < pollStatus.size());
            if (pollStatus.at(*fdPollStatusId).revents) {
                ssize_t rd = ::read(k, buffer.data(), buffer.size());
                // FIXME: is there a cleaner way to handle pt close
                // than EIO? Is this even standard?
                if (rd == 0 || (rd == -1 && errno == EIO)) {
                    debug("%1%: got EOF", goal->getName());
                    goal->handleEOF(k);
                    j->fds.erase(k);
                } else if (rd == -1) {
                    if (errno != EINTR)
                        throw SysError("%s: read failed", goal->getName());
                } else {
                    printMsg(lvlVomit, "%1%: read %2% bytes",
                        goal->getName(), rd);
                    std::string data((char *) buffer.data(), rd);
                    j->lastOutput = after;
                    goal->handleChildOutput(k, data);
                }
            }
        }

        if (goal->exitCode == Goal::ecBusy &&
            0 != settings.maxSilentTime &&
            j->respectTimeouts &&
            after - j->lastOutput >= std::chrono::seconds(settings.maxSilentTime))
        {
            goal->timedOut(Error(
                    "%1% timed out after %2% seconds of silence",
                    goal->getName(), settings.maxSilentTime));
        }

        else if (goal->exitCode == Goal::ecBusy &&
            0 != settings.buildTimeout &&
            j->respectTimeouts &&
            after - j->timeStarted >= std::chrono::seconds(settings.buildTimeout))
        {
            goal->timedOut(Error(
                    "%1% timed out after %2% seconds",
                    goal->getName(), settings.buildTimeout));
        }
    }

    if (!waitingForAWhile.empty() && lastWokenUp + std::chrono::seconds(settings.pollInterval) <= after) {
        lastWokenUp = after;
        for (auto & i : waitingForAWhile) {
            GoalPtr goal = i.lock();
            if (goal) wakeUp(goal);
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
        mask |= 0x04;  // 100
    if (timedOut)
        mask |= 0x01;  // 101
    if (hashMismatch)
        mask |= 0x02;  // 102
    if (checkMismatch) {
        mask |= 0x08;  // 104
    }

    if (mask)
        mask |= 0x60;
    return mask ? mask : 1;
}


bool Worker::pathContentsGood(const StorePath & path)
{
    auto i = pathContentsGoodCache.find(path);
    if (i != pathContentsGoodCache.end()) return i->second;
    printInfo("checking path '%s'...", store.printStorePath(path));
    auto info = store.queryPathInfo(path);
    bool res;
    if (!pathExists(store.printStorePath(path)))
        res = false;
    else {
        HashResult current = hashPath(info->narHash.type, store.printStorePath(path));
        Hash nullHash(htSHA256);
        res = info->narHash == nullHash || info->narHash == current.first;
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

const DerivationGoal * tryGetConcreteDrvGoal(GoalPtr waitee)
{
    auto * odg = dynamic_cast<CreateDerivationAndRealiseGoal *>(&*waitee);
    if (!odg) return nullptr;
    return &*odg->concreteDrvGoal;
}

}
