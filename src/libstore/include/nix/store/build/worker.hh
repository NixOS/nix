#pragma once
///@file

#include "nix/util/types.hh"
#include "nix/store/store-api.hh"
#include "nix/store/derived-path-map.hh"
#include "nix/store/build/goal.hh"
#include "nix/store/realisation.hh"
#include "nix/util/muxable-pipe.hh"

#include <future>
#include <thread>

namespace nix {

/* Forward definition. */
struct DerivationTrampolineGoal;
struct DerivationGoal;
struct DerivationResolutionGoal;
struct DerivationBuildingGoal;
struct PathSubstitutionGoal;
class DrvOutputSubstitutionGoal;

/**
 * Workaround for not being able to declare a something like
 *
 * ```c++
 * class PathSubstitutionGoal : public Goal;
 * ```
 * even when Goal is a complete type.
 *
 * This is still a static cast. The purpose of exporting it is to define it in
 * a place where `PathSubstitutionGoal` is concrete, and use it in a place where it
 * is opaque.
 */
GoalPtr upcast_goal(std::shared_ptr<PathSubstitutionGoal> subGoal);
GoalPtr upcast_goal(std::shared_ptr<DrvOutputSubstitutionGoal> subGoal);
GoalPtr upcast_goal(std::shared_ptr<DerivationGoal> subGoal);

typedef std::chrono::time_point<std::chrono::steady_clock> steady_time_point;

/**
 * A mapping used to remember for each child process to what goal it
 * belongs, and comm channels for receiving log data and output
 * path creation commands.
 */
struct Child
{
    WeakGoalPtr goal;
    Goal * goal2; // ugly hackery
    std::set<MuxablePipePollState::CommChannel> channels;
    bool respectTimeouts;
    bool inBuildSlot;
    /**
     * Time we last got output on stdout/stderr
     */
    steady_time_point lastOutput;
    steady_time_point timeStarted;
};

#ifndef _WIN32 // TODO Enable building on Windows
/* Forward definition. */
struct HookInstance;
#endif

/**
 * Coordinates one or more realisations and their interdependencies.
 */
class Worker
{
private:

    /* Note: the worker should only have strong pointers to the
       top-level goals. */

    /**
     * The top-level goals of the worker.
     */
    Goals topGoals;

    /**
     * Goals that are ready to do some work.
     */
    WeakGoals awake;

    /**
     * Goals waiting for a build slot.
     */
    WeakGoals wantingToBuild;

    /**
     * Child processes currently running.
     */
    std::list<Child> children;

    /**
     * Number of build slots occupied.  This includes local builds but does not
     * include substitutions or remote builds via the build hook.
     */
    size_t nrLocalBuilds;

    /**
     * Number of substitution slots occupied.
     */
    size_t nrSubstitutions;

    /**
     * Maps used to prevent multiple instantiations of a goal for the
     * same derivation / path.
     */

    DerivedPathMap<std::map<OutputsSpec, std::weak_ptr<DerivationTrampolineGoal>>> derivationTrampolineGoals;

    std::map<StorePath, std::map<OutputName, std::weak_ptr<DerivationGoal>>> derivationGoals;
    std::map<StorePath, std::weak_ptr<DerivationResolutionGoal>> derivationResolutionGoals;
    std::map<StorePath, std::weak_ptr<DerivationBuildingGoal>> derivationBuildingGoals;
    std::map<StorePath, std::weak_ptr<PathSubstitutionGoal>> substitutionGoals;
    std::map<DrvOutput, std::weak_ptr<DrvOutputSubstitutionGoal>> drvOutputSubstitutionGoals;

    /**
     * Goals waiting for busy paths to be unlocked.
     */
    WeakGoals waitingForAnyGoal;

    /**
     * Goals sleeping for a few seconds (polling a lock).
     */
    WeakGoals waitingForAWhile;

    /**
     * Last time the goals in `waitingForAWhile` were woken up.
     */
    steady_time_point lastWokenUp;

    /**
     * Cache for pathContentsGood().
     */
    std::map<StorePath, bool> pathContentsGoodCache;

public:

    const Activity act;
    const Activity actDerivations;
    const Activity actSubstitutions;

    /**
     * Set if at least one derivation had a BuildError (i.e. permanent
     * failure).
     */
    bool permanentFailure;

    /**
     * Set if at least one derivation had a timeout.
     */
    bool timedOut;

    /**
     * Set if at least one derivation fails with a hash mismatch.
     */
    bool hashMismatch;

    /**
     * Set if at least one derivation is not deterministic in check mode.
     */
    bool checkMismatch;

#ifdef _WIN32
    AutoCloseFD ioport;
#endif

    Store & store;
    Store & evalStore;

#ifndef _WIN32 // TODO Enable building on Windows
    std::unique_ptr<HookInstance> hook;
#endif

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

    /**
     * Whether to ask the build hook if it can build a derivation. If
     * it answers with "decline-permanently", we don't try again.
     */
    bool tryBuildHook = true;

    Worker(Store & store, Store & evalStore);
    ~Worker();

    /**
     * Make a goal (with caching).
     */

    /**
     * @ref DerivationGoal "derivation goal"
     */
private:
    template<class G, typename... Args>
    std::shared_ptr<G> initGoalIfNeeded(std::weak_ptr<G> & goal_weak, Args &&... args);

    std::shared_ptr<DerivationTrampolineGoal> makeDerivationTrampolineGoal(
        ref<const SingleDerivedPath> drvReq, const OutputsSpec & wantedOutputs, BuildMode buildMode);

public:
    std::shared_ptr<DerivationTrampolineGoal> makeDerivationTrampolineGoal(
        const StorePath & drvPath, const OutputsSpec & wantedOutputs, const Derivation & drv, BuildMode buildMode);

    std::shared_ptr<DerivationGoal> makeDerivationGoal(
        const StorePath & drvPath,
        const Derivation & drv,
        const OutputName & wantedOutput,
        BuildMode buildMode,
        bool storeDerivation);

    /**
     * @ref DerivationResolutionGoal "derivation resolution goal"
     */
    std::shared_ptr<DerivationResolutionGoal>
    makeDerivationResolutionGoal(const StorePath & drvPath, const Derivation & drv, BuildMode buildMode);

    /**
     * @ref DerivationBuildingGoal "derivation building goal"
     */
    std::shared_ptr<DerivationBuildingGoal> makeDerivationBuildingGoal(
        const StorePath & drvPath, const Derivation & drv, BuildMode buildMode, bool storeDerivation);

    /**
     * @ref PathSubstitutionGoal "substitution goal"
     */
    std::shared_ptr<PathSubstitutionGoal> makePathSubstitutionGoal(
        const StorePath & storePath, RepairFlag repair = NoRepair, std::optional<ContentAddress> ca = std::nullopt);
    std::shared_ptr<DrvOutputSubstitutionGoal> makeDrvOutputSubstitutionGoal(const DrvOutput & id);

    /**
     * Make a goal corresponding to the `DerivedPath`.
     *
     * It will be a `DerivationGoal` for a `DerivedPath::Built` or
     * a `PathSubstitutionGoal` for a `DerivedPath::Opaque`.
     */
    GoalPtr makeGoal(const DerivedPath & req, BuildMode buildMode = bmNormal);

    /**
     * Remove a dead goal.
     */
    void removeGoal(GoalPtr goal);

    /**
     * Wake up a goal (i.e., there is something for it to do).
     */
    void wakeUp(GoalPtr goal);

    /**
     * Return the number of local build processes currently running (but not
     * remote builds via the build hook).
     */
    size_t getNrLocalBuilds();

    /**
     * Return the number of substitution processes currently running.
     */
    size_t getNrSubstitutions();

    /**
     * Registers a running child process.  `inBuildSlot` means that
     * the process counts towards the jobs limit.
     */
    void childStarted(
        GoalPtr goal,
        const std::set<MuxablePipePollState::CommChannel> & channels,
        bool inBuildSlot,
        bool respectTimeouts);

    /**
     * Unregisters a running child process.  `wakeSleepers` should be
     * false if there is no sense in waking up goals that are sleeping
     * because they can't run yet (e.g., there is no free build slot,
     * or the hook would still say `postpone`).
     */
    void childTerminated(Goal * goal, bool wakeSleepers = true);

    /**
     * Put `goal` to sleep until a build slot becomes available (which
     * might be right away).
     */
    void waitForBuildSlot(GoalPtr goal);

    /**
     * Wait for any goal to finish.  Pretty indiscriminate way to
     * wait for some resource that some other goal is holding.
     */
    void waitForAnyGoal(GoalPtr goal);

    /**
     * Wait for a few seconds and then retry this goal.  Used when
     * waiting for a lock held by another process.  This kind of
     * polling is inefficient, but POSIX doesn't really provide a way
     * to wait for multiple locks in the main select() loop.
     */
    void waitForAWhile(GoalPtr goal);

    /**
     * Loop until the specified top-level goals have finished.
     */
    void run(const Goals & topGoals);

    /**
     * Wait for input to become available.
     */
    void waitForInput();

    /***
     * The exit status in case of failure.
     *
     * In the case of a build failure, returned value follows this
     * bitmask:
     *
     * ```
     * 0b1100100
     *      ^^^^
     *      |||`- timeout
     *      ||`-- output hash mismatch
     *      |`--- build failure
     *      `---- not deterministic
     * ```
     *
     * In other words, the failure code is at least 100 (0b1100100), but
     * might also be greater.
     *
     * Otherwise (no build failure, but some other sort of failure by
     * assumption), this returned value is 1.
     */
    unsigned int failingExitStatus();

    /**
     * Check whether the given valid path exists and has the right
     * contents.
     */
    bool pathContentsGood(const StorePath & path);

    void markContentsGood(const StorePath & path);

    void updateProgress()
    {
        actDerivations.progress(doneBuilds, expectedBuilds + doneBuilds, runningBuilds, failedBuilds);
        actSubstitutions.progress(
            doneSubstitutions, expectedSubstitutions + doneSubstitutions, runningSubstitutions, failedSubstitutions);
        act.setExpected(actFileTransfer, expectedDownloadSize + doneDownloadSize);
        act.setExpected(actCopyPath, expectedNarSize + doneNarSize);
    }
};

} // namespace nix
