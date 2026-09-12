#pragma once
///@file

#include "nix/util/types.hh"
#include "nix/store/store-api.hh"
#include "nix/store/build.hh"
#include "nix/store/derived-path-map.hh"
#include "nix/store/build/goal.hh"
#include "nix/store/machines.hh"
#include "nix/store/build-result.hh"
#include "nix/store/realisation.hh"
#include "nix/util/async.hh"

#include <boost/asio/strand.hpp>
#include <boost/asio/thread_pool.hpp>
#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/error.hpp>

#include <deque>
#include <filesystem>
#include <functional>
#include <future>
#include <mutex>
#include <thread>
#include <queue>

namespace nix {

/**
 * A counting semaphore for coroutines. Not thread-safe by itself: all
 * operations must run on the executor it was created with, which is the
 * worker's strand.
 */
class AsyncSemaphore
{
    asio::any_io_executor ex;

    /**
     * Number of slots that can be acquired right now.
     */
    std::size_t available;

    /**
     * Acquirers waiting for a slot, in FIFO order. A waiter that has been
     * cancelled is a null function and is skipped.
     */
    std::deque<std::move_only_function<void()>> waiters;

    void release()
    {
        while (!waiters.empty()) {
            auto waiter = std::move(waiters.front());
            waiters.pop_front();
            if (waiter) {
                asio::post(ex, std::move(waiter));
                return;
            }
        }
        ++available;
    }

public:
    AsyncSemaphore(asio::any_io_executor ex, std::size_t initialCount)
        : ex(std::move(ex))
        , available(initialCount)
    {
    }

    /**
     * An acquired slot; releases it when destroyed.
     */
    struct Handle
    {
        friend class AsyncSemaphore;
        AsyncSemaphore * sem = nullptr;

        Handle(AsyncSemaphore & sem)
            : sem(&sem)
        {
        }

    public:

        Handle(Handle && other) noexcept
            : sem(std::exchange(other.sem, nullptr))
        {
        }

        Handle & operator=(Handle && other) noexcept
        {
            if (this == &other)
                return *this;
            if (sem)
                sem->release();
            sem = std::exchange(other.sem, nullptr);
            return *this;
        }

        Handle(const Handle &) = delete;
        Handle & operator=(const Handle &) = delete;

        ~Handle()
        {
            if (sem)
                sem->release();
        }
    };

    /**
     * Acquire a slot if one is free right now.
     */
    std::optional<Handle> tryAcquire()
    {
        if (available == 0)
            return std::nullopt;
        --available;
        return Handle(*this);
    }

    /**
     * Whether @ref tryAcquire would succeed right now.
     */
    bool canAcquireNow() const noexcept
    {
        return available > 0;
    }

    /**
     * Wait for a slot to become free and acquire it.
     */
    asio::awaitable<Handle> asyncAcquire()
    {
        if (auto handle = tryAcquire())
            co_return std::move(*handle);

        co_await asio::async_initiate<decltype(asio::use_awaitable), void(boost::system::error_code)>(
            [&](auto handler) {
                using Handler = std::decay_t<decltype(handler)>;
                auto state = std::make_shared<std::optional<Handler>>(std::move(handler));
                auto slot = asio::get_associated_cancellation_slot(**state);

                waiters.push_back([state] {
                    if (auto h = std::exchange(*state, std::nullopt))
                        (*std::move(h))(boost::system::error_code{});
                });

                if (slot.is_connected())
                    slot.assign([state, ex = ex](asio::cancellation_type) {
                        if (auto h = std::exchange(*state, std::nullopt))
                            asio::post(ex, [h = std::move(*h)]() mutable {
                                std::move(h)(boost::system::error_code(asio::error::operation_aborted));
                            });
                    });
            },
            asio::use_awaitable);

        co_return Handle(*this);
    }

    /**
     * Add a slot, for the duration until @ref reclaim is called. Used to
     * let a build that is itself waiting on nested builds hand its slot
     * down to them.
     */
    void lend()
    {
        release();
    }

    /**
     * Take back a slot added by @ref lend, waiting for one to be released
     * if none is free right now.
     */
    void reclaim()
    {
        if (available > 0) {
            --available;
            return;
        }
        waiters.push_back([] {});
    }
};

/* Forward definition. */
struct WorkerSettings;
struct DerivationTrampolineGoal;
struct DerivationGoal;
struct DerivationResolutionGoal;
struct DerivationBuildingGoal;
class PathSubstitutionGoal;
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

/**
 * Coordinates one or more realisations and their interdependencies:
 * the local build scheduler, and the `Builder` for local stores.
 *
 * Thread safety: the `Builder` methods may be called concurrently from
 * any thread. All goal state lives on a single strand, `ex`, of an
 * `io_context` that the worker runs on its own fixed pool of threads,
 * so goals are serialised with respect to each other while I/O,
 * timers and completions are serviced in parallel. Only the strand ever
 * touches goals, the goal maps, or the counters below.
 *
 * @todo Rename to `LocalBuilder`.
 */
class Worker : public Builder
{
public:
    const WorkerSettings & settings;

private:
    friend struct Goal;

    /* Note: the worker should only have strong pointers to the
       top-level goals. */

    asio::io_context ioContext;

    /**
     * Executor on which all the coroutines are run.
     */
    asio::strand<asio::any_io_executor> ex = asio::make_strand(ioContext.get_executor());

    /**
     * How many threads run `ioContext`.
     */
    static constexpr std::size_t nrIoThreads = 4;

    /**
     * The threads running `ioContext`, started lazily by @ref ensureRunning
     * so that a worker that never has anything to do costs nothing.
     */
    std::vector<std::thread> ioThreads;

    /**
     * Keeps `ioContext` alive between runs. Reset by the destructor to let
     * the threads finish.
     */
    std::optional<asio::executor_work_guard<asio::io_context::executor_type>> workGuard;

    /**
     * Guards the lazy start of @ref ioThreads.
     */
    std::mutex startMutex;

    /**
     * Number of @ref run calls currently in progress. Strand-only.
     */
    std::size_t activeRuns = 0;

    /**
     * Stops the background auto-GC loop, which runs while @ref activeRuns
     * is non-zero. Strand-only.
     */
    std::shared_ptr<asio::cancellation_signal> stopAutoGC;

    /**
     * Start the I/O threads if they are not running yet.
     */
    void ensureRunning();

    /**
     * The periodic auto-GC loop; see @ref run.
     */
    asio::awaitable<void> autoGCLoop();

    /**
     * Thread pool to run blocking work on.
     */
    asio::thread_pool threadPool;

    /**
     * The top-level goals of all @ref run calls in progress. Strand-only.
     */
    Goals topGoals;

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
     * Cache for pathContentsGood().
     */
    std::map<StorePath, bool> pathContentsGoodCache;

public:

    auto & getThreadPool()
    {
        return threadPool;
    }

    /**
     * Semaphore limiting acquired build slots.
     */
    AsyncSemaphore buildSemaphore;

    /**
     * Semaphore limiting acquired substitution slots.
     */
    AsyncSemaphore substitutionSemaphore;

    const Activity act;
    const Activity actDerivations;
    const Activity actSubstitutions;

    /**
     * Tracks different types of build failures for exit status computation.
     */
    ExitStatusFlags exitStatusFlags;

private:
    ref<Store> storeRef, evalStoreRef;

public:
    /**
     * Aliases of @ref storeRef and @ref evalStoreRef.
     */
    Store & store;
    Store & evalStore;

    /**
     * Function to get the substituters to use for path substitution.
     *
     * Defaults to `getDefaultSubstituters`. This allows tests to
     * inject custom substituters.
     */
    fun<std::list<ref<Store>>()> getSubstituters;

    /**
     * Function to get the remote builders to use, as the build hook
     * (`nix __build-remote`) used to. Defaults to parsing the `builders`
     * setting. This allows tests to inject custom builders.
     */
    fun<Machines()> getRemoteBuilders;

private:
    /**
     * The remote builders, obtained from @ref getRemoteBuilders on first
     * use and kept, since goals hold references into the list and
     * disable machines they cannot connect to. Strand-only.
     */
    std::optional<Machines> remoteBuilders;

public:
    /**
     * Directory holding the lock files that track the load of the
     * remote builders (one per slot) and serialise uploads to them, so
     * that concurrent workers, in this process or others, see each
     * other's builds. See `DerivationBuildingGoal`.
     */
    const std::filesystem::path currentLoad;

    /**
     * The remote builders (see @ref getRemoteBuilders). Strand-only.
     */
    Machines & machines();

public:

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

    Worker(ref<Store> store, ref<Store> evalStore);
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
        ref<const Derivation> drv,
        const OutputName & wantedOutput,
        BuildMode buildMode,
        bool storeDerivation);

    /**
     * @ref DerivationResolutionGoal "derivation resolution goal"
     */
    std::shared_ptr<DerivationResolutionGoal>
    makeDerivationResolutionGoal(const StorePath & drvPath, ref<const Derivation> drv, BuildMode buildMode);

    /**
     * @ref DerivationBuildingGoal "derivation building goal"
     */
    std::shared_ptr<DerivationBuildingGoal>
    makeDerivationBuildingGoal(const StorePath & drvPath, ref<const BasicDerivation> drv, BuildMode buildMode);

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
     * Run `body` on the worker's strand and block until it has finished,
     * rethrowing any exception it threw. This is the only way in: goals
     * must be created, awaited (see @ref awaitTopGoals) and inspected
     * inside `body`, since all of that touches strand-only state.
     *
     * Interrupting the calling thread cancels this call's goals only.
     *
     * Must not be called from the strand itself, i.e. from inside a
     * goal, since that would block the very thread that has to run it.
     */
    void run(fun<asio::awaitable<void>()> body);

    /**
     * Await a set of top-level goals, cancelling the rest on the first
     * failure unless `keep-going` is set. To be called from within
     * @ref run.
     */
    asio::awaitable<void> awaitTopGoals(Goals goals);

    /**
     * Lend the build slot of a running build to the builds it requests
     * recursively (see `DerivationBuilderCallbacks::processDaemonConnection`),
     * and take it back. Thread-safe.
     */
    void lendBuildSlot();
    void reclaimBuildSlot();

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

    /* Builder interface — see `Builder` for documentation. */

    void buildPaths(const std::vector<DerivedPath> & reqs, BuildMode buildMode) override;
    std::vector<KeyedBuildResult>
    buildPathsWithResults(const std::vector<DerivedPath> & reqs, BuildMode buildMode) override;
    BuildResult buildDerivation(const StorePath & drvPath, const BasicDerivation & drv, BuildMode buildMode) override;
    void ensurePath(const StorePath & path) override;
    void repairPath(const StorePath & path) override;
};

} // namespace nix
