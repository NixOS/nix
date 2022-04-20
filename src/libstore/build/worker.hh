#pragma once

#include "types.hh"
#include "lock.hh"
#include "store-api.hh"
#include "goal.hh"
#include "realisation.hh"

#include <future>
#include <thread>

namespace nix {

/* Forward definition. */
struct DerivationGoal;
struct PathSubstitutionGoal;
class DrvOutputSubstitutionGoal;

/* Workaround for not being able to declare a something like

     class PathSubstitutionGoal : public Goal;

   even when Goal is a complete type.

   This is still a static cast. The purpose of exporting it is to define it in
   a place where `PathSubstitutionGoal` is concrete, and use it in a place where it
   is opaque. */
GoalPtr upcast_goal(std::shared_ptr<PathSubstitutionGoal> subGoal);
GoalPtr upcast_goal(std::shared_ptr<DrvOutputSubstitutionGoal> subGoal);

typedef std::chrono::time_point<std::chrono::steady_clock> steady_time_point;


/* A mapping used to remember for each child process to what goal it
   belongs, and file descriptors for receiving log data and output
   path creation commands. */
struct Child
{
    WeakGoalPtr goal;
    Goal * goal2; // ugly hackery
    std::set<int> fds;
    bool respectTimeouts;
    bool inBuildSlot;
    steady_time_point lastOutput; /* time we last got output on stdout/stderr */
    steady_time_point timeStarted;
};

/* Forward definition. */
struct HookInstance;

/* The worker class. */
class Worker
{
private:

    /* Note: the worker should only have strong pointers to the
       top-level goals. */

    /* The top-level goals of the worker. */
    Goals topGoals;

    /* Goals that are ready to do some work. */
    WeakGoals awake;

    /* Goals waiting for a build slot. */
    WeakGoals wantingToBuild;

    /* Child processes currently running. */
    std::list<Child> children;

    /* Number of build slots occupied.  This includes local builds and
       substitutions but not remote builds via the build hook. */
    unsigned int nrLocalBuilds;

    /* Maps used to prevent multiple instantiations of a goal for the
       same derivation / path. */
    std::map<StorePath, std::weak_ptr<DerivationGoal>> derivationGoals;
    std::map<StorePath, std::weak_ptr<PathSubstitutionGoal>> substitutionGoals;
    std::map<DrvOutput, std::weak_ptr<DrvOutputSubstitutionGoal>> drvOutputSubstitutionGoals;

    /* Goals waiting for busy paths to be unlocked. */
    WeakGoals waitingForAnyGoal;

    /* Goals sleeping for a few seconds (polling a lock). */
    WeakGoals waitingForAWhile;

    /* Last time the goals in `waitingForAWhile' where woken up. */
    steady_time_point lastWokenUp;

    /* Cache for pathContentsGood(). */
    std::map<StorePath, bool> pathContentsGoodCache;

public:

    const Activity act;
    const Activity actDerivations;
    const Activity actSubstitutions;

    /* Set if at least one derivation had a BuildError (i.e. permanent
       failure). */
    bool permanentFailure;

    /* Set if at least one derivation had a timeout. */
    bool timedOut;

    /* Set if at least one derivation fails with a hash mismatch. */
    bool hashMismatch;

    /* Set if at least one derivation is not deterministic in check mode. */
    bool checkMismatch;

    Store & store;
    Store & evalStore;

    std::unique_ptr<HookInstance> hook;

    uint64_t expectedBuilds = 0;
    uint64_t doneBuilds = 0;
    uint64_t failedBuilds = 0;
    uint64_t runningBuilds = 0;

    uint64_t expectedSubstitutions = 0;
    uint64_t doneSubstitutions = 0;
    uint64_t failedSubstitutions = 0;
    uint64_t runningSubstitutions = 0;
    uint64_t expectedDownloadSize = 0;
    uint64_t doneDownloadSize = 0;
    uint64_t expectedNarSize = 0;
    uint64_t doneNarSize = 0;

    /* Whether to ask the build hook if it can build a derivation. If
       it answers with "decline-permanently", we don't try again. */
    bool tryBuildHook = true;

    Worker(Store & store, Store & evalStore);
    ~Worker();

    /* Make a goal (with caching). */

    /* derivation goal */
private:
    std::shared_ptr<DerivationGoal> makeDerivationGoalCommon(
        const StorePath & drvPath, const StringSet & wantedOutputs,
        std::function<std::shared_ptr<DerivationGoal>()> mkDrvGoal);
public:
    std::shared_ptr<DerivationGoal> makeDerivationGoal(
        const StorePath & drvPath,
        const StringSet & wantedOutputs, BuildMode buildMode = bmNormal);
    std::shared_ptr<DerivationGoal> makeBasicDerivationGoal(
        const StorePath & drvPath, const BasicDerivation & drv,
        const StringSet & wantedOutputs, BuildMode buildMode = bmNormal);

    /* substitution goal */
    std::shared_ptr<PathSubstitutionGoal> makePathSubstitutionGoal(StorePathOrDesc storePath, RepairFlag repair = NoRepair);
    std::shared_ptr<DrvOutputSubstitutionGoal> makeDrvOutputSubstitutionGoal(const DrvOutput & id, RepairFlag repair = NoRepair);

    /* Remove a dead goal. */
    void removeGoal(GoalPtr goal);

    /* Wake up a goal (i.e., there is something for it to do). */
    void wakeUp(GoalPtr goal);

    /* Return the number of local build and substitution processes
       currently running (but not remote builds via the build
       hook). */
    unsigned int getNrLocalBuilds();

    /* Registers a running child process.  `inBuildSlot' means that
       the process counts towards the jobs limit. */
    void childStarted(GoalPtr goal, const std::set<int> & fds,
        bool inBuildSlot, bool respectTimeouts);

    /* Unregisters a running child process.  `wakeSleepers' should be
       false if there is no sense in waking up goals that are sleeping
       because they can't run yet (e.g., there is no free build slot,
       or the hook would still say `postpone'). */
    void childTerminated(Goal * goal, bool wakeSleepers = true);

    /* Put `goal' to sleep until a build slot becomes available (which
       might be right away). */
    void waitForBuildSlot(GoalPtr goal);

    /* Wait for any goal to finish.  Pretty indiscriminate way to
       wait for some resource that some other goal is holding. */
    void waitForAnyGoal(GoalPtr goal);

    /* Wait for a few seconds and then retry this goal.  Used when
       waiting for a lock held by another process.  This kind of
       polling is inefficient, but POSIX doesn't really provide a way
       to wait for multiple locks in the main select() loop. */
    void waitForAWhile(GoalPtr goal);

    /* Loop until the specified top-level goals have finished. */
    void run(const Goals & topGoals);

    /* Wait for input to become available. */
    void waitForInput();

    unsigned int exitStatus();

    /* Check whether the given valid path exists and has the right
       contents. */
    bool pathContentsGood(const StorePath & path);

    void markContentsGood(const StorePath & path);

    void updateProgress()
    {
        actDerivations.progress(doneBuilds, expectedBuilds + doneBuilds, runningBuilds, failedBuilds);
        actSubstitutions.progress(doneSubstitutions, expectedSubstitutions + doneSubstitutions, runningSubstitutions, failedSubstitutions);
        act.setExpected(actFileTransfer, expectedDownloadSize + doneDownloadSize);
        act.setExpected(actCopyPath, expectedNarSize + doneNarSize);
    }
};

}
