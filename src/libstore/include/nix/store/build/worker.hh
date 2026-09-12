#pragma once
///@file

#include "nix/util/types.hh"
#include "nix/store/store-api.hh"
#include "nix/store/build.hh"
#include "nix/store/derived-path-map.hh"
#include "nix/store/build/goal.hh"
#include "nix/store/build-result.hh"
#include "nix/store/realisation.hh"
#include "nix/util/async.hh"

#include <boost/asio/strand.hpp>
#include <boost/asio/thread_pool.hpp>
#include <boost/asio/experimental/channel.hpp>

#include <functional>
#include <future>
#include <thread>
#include <queue>

namespace nix {

class AsyncSemaphore
{
    asio::experimental::channel<asio::any_io_executor, void(boost::system::error_code, int)> channel;

    void release()
    {
        channel.try_send(boost::system::error_code{}, 42);
    }

public:
    AsyncSemaphore(asio::any_io_executor ex, std::size_t initialCount)
        : channel(ex, initialCount)
    {
        channel.try_send_n(initialCount, boost::system::error_code{}, 42);
    }

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
            if (sem) {
                sem->release();
                sem = nullptr;
            }
            sem = std::exchange(other.sem, nullptr);
            return *this;
        }

        Handle(const Handle &);

        Handle & operator=(const Handle &) = delete;

        ~Handle()
        {
            if (sem) {
                sem->release();
                sem = nullptr;
            }
        }
    };

    asio::awaitable<Handle> asyncAcquire()
    {
        co_await channel.async_receive(asio::use_awaitable);
        co_return Handle(*this);
    }

    /**
     * Acquire a slot if one is free right now.
     */
    std::optional<Handle> tryAcquire()
    {
        if (channel.try_receive([](boost::system::error_code, int) {}))
            return Handle(*this);
        return std::nullopt;
    }

    /**
     * Whether @ref tryAcquire would succeed right now.
     */
    bool canAcquireNow() const noexcept
    {
        return channel.ready();
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

#ifndef _WIN32 // TODO Enable building on Windows
/* Forward definition. */
struct HookInstance;
#endif

/**
 * Owns a worker. Optimization around ensurePath to prevent a Worker from
 * being constructed when it's not needed.
 */
class LocalBuilder : public Builder
{
public:
    LocalBuilder(ref<Store> store, ref<Store> evalStore)
        : store(store)
        , evalStore(evalStore) {};

    /* Builder interface — see `Builder` for documentation. */

    void buildPaths(const std::vector<DerivedPath> & reqs, BuildMode buildMode) override;
    std::vector<KeyedBuildResult>
    buildPathsWithResults(const std::vector<DerivedPath> & reqs, BuildMode buildMode) override;
    BuildResult buildDerivation(const StorePath & drvPath, const BasicDerivation & drv, BuildMode buildMode) override;
    void ensurePath(const StorePath & path) override;
    void repairPath(const StorePath & path) override;

private:
    /**
     * Intentionally construct a new worker for each operation, to avoid
     * reusing a worker between calls, allowing for thread safety.
     */
    inline std::shared_ptr<Worker> getWorker()
    {
        return std::make_shared<Worker>(*store, *evalStore);
    }

    ref<Store> store;
    ref<Store> evalStore;
};

/**
 * Coordinates one or more realisations and their interdependencies.
 */
class Worker : public Builder
{
public:
    const WorkerSettings & settings;

private:
    friend struct Goal;

    /* Note: the worker should only have strong pointers to the
       top-level goals. */

    /* TODO: Once we are more done with asyncification, this should be
       be gone and the executer should be a strand on a shared event loop. */
    asio::io_context ioContext;

    /**
     * Executer on which all the coroutines are run.
     */
    asio::strand<asio::any_io_executor> ex = asio::make_strand(ioContext.get_executor());

    /**
     * Thread pool to run blocking work on.
     */
    asio::thread_pool threadPool;

    /**
     * The top-level goals of the worker.
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

    Store & store;
    Store & evalStore;

    /**
     * Function to get the substituters to use for path substitution.
     *
     * Defaults to `getDefaultSubstituters`. This allows tests to
     * inject custom substituters.
     */
    fun<std::list<ref<Store>>()> getSubstituters;

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

    asio::awaitable<void> awaitTopGoals();

    /**
     * Loop until the specified top-level goals have finished.
     */
    void run(const Goals & topGoals);

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
