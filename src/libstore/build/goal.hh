#pragma once
///@file

#include "store-api.hh"
#include "build-result.hh"

#include <coroutine>

namespace nix {

/**
 * Forward definition.
 */
struct Goal;
class Worker;

/**
 * A pointer to a goal.
 */
typedef std::shared_ptr<Goal> GoalPtr;
typedef std::weak_ptr<Goal> WeakGoalPtr;

struct CompareGoalPtrs {
    bool operator() (const GoalPtr & a, const GoalPtr & b) const;
};

/**
 * Set of goals.
 */
typedef std::set<GoalPtr, CompareGoalPtrs> Goals;
typedef std::set<WeakGoalPtr, std::owner_less<WeakGoalPtr>> WeakGoals;

/**
 * A map of paths to goals (and the other way around).
 */
typedef std::map<StorePath, WeakGoalPtr> WeakGoalMap;

/**
 * Used as a hint to the worker on how to schedule a particular goal. For example,
 * builds are typically CPU- and memory-bound, while substitutions are I/O bound.
 * Using this information, the worker might decide to schedule more or fewer goals
 * of each category in parallel.
 */
enum struct JobCategory {
    /**
     * A build of a derivation; it will use CPU and disk resources.
     */
    Build,
    /**
     * A substitution an arbitrary store object; it will use network resources.
     */
    Substitution,
};

struct Goal : public std::enable_shared_from_this<Goal>
{
    typedef enum {ecBusy, ecSuccess, ecFailed, ecNoSubstituters, ecIncompleteClosure} ExitCode;

    /**
     * Backlink to the worker.
     */
    Worker & worker;

    /**
     * Goals that this goal is waiting for.
     */
    Goals waitees;

    /**
     * Goals waiting for this one to finish.  Must use weak pointers
     * here to prevent cycles.
     */
    WeakGoals waiters;

    /**
     * Number of goals we are/were waiting for that have failed.
     */
    size_t nrFailed = 0;

    /**
     * Number of substitution goals we are/were waiting for that
     * failed because there are no substituters.
     */
    size_t nrNoSubstituters = 0;

    /**
     * Number of substitution goals we are/were waiting for that
     * failed because they had unsubstitutable references.
     */
    size_t nrIncompleteClosure = 0;

    /**
     * Name of this goal for debugging purposes.
     */
    std::string name;

    /**
     * Whether the goal is finished.
     */
    ExitCode exitCode = ecBusy;

public:
    /**
     * Build result.
     */
    BuildResult buildResult;

    /*
     * Suspend our goal and wait until we get work()-ed again.
     */
    struct SuspendGoal {};
    struct SuspendGoalAwaiter {
        bool wait;
        bool await_ready() { return !wait; }
        void await_suspend(std::coroutine_handle<> handle) {}
        void await_resume() {}
    };
    struct [[nodiscard]] Done {
        private:
        Done() {}
    };
    struct promise_type;
    using handle_type = std::coroutine_handle<promise_type>;
    // FIXME: Allocate explicitly on stack since HALO thing doesn't really work,
    // specifically, there's no way to uphold the requirements when trying to do
    // tail-calls without using a trampoline AFAICT.
    // NOTES:
    // These are good resources for understanding how coroutines work:
    // https://lewissbaker.github.io/
    // https://www.chiark.greenend.org.uk/~sgtatham/quasiblog/coroutines-c++20/
    // https://www.scs.stanford.edu/~dm/blog/c++-coroutines.html
    struct [[nodiscard]] Co {
        handle_type handle;
        explicit Co(handle_type handle) : handle(handle) {};
        Co(const Co&) = delete;
        Co &operator=(const Co&) = delete;
        void operator=(Co&&);
        Co(Co&& rhs);
        ~Co();

        bool await_ready() { return false; };
        std::coroutine_handle<> await_suspend(handle_type handle);
        void await_resume() {};
    };
    struct promise_type {
        // Either this is who called us, or it is who we will tail-call.
        // It is what we "jump" to once we are done.
        std::optional<Co> continuation;
        Goal& goal;
        bool alive = true;

        template<typename... ArgTypes>
        promise_type(Goal& goal, ArgTypes...) : goal(goal) {}

        struct final_awaiter {
            bool await_ready() noexcept { return false; };;
            std::coroutine_handle<> await_suspend(handle_type) noexcept;
            void await_resume() noexcept { assert(false); };
        };
        Co get_return_object();
        std::suspend_always initial_suspend() { 
            // top_co isn't set to us yet,
            // we've merely constructed the frame and now the
            // caller is free to do whatever they wish to us.
            return {};
        };
        final_awaiter final_suspend() noexcept { return {}; };
        // Same as returning void, but makes it clear that we're done.
        // Should ideally call `done` rather than `done` giving you a `Done`.
        // `final_suspend` will be called after this (since we're returning),
        // and that will give us a `final_awaiter`, which will stop the coroutine
        // from executing since `exitCode != ecBusy`.
        void return_value(Done) {
            assert(goal.exitCode != ecBusy);
        }
        void return_value(Co&&);
        void unhandled_exception() { throw; };

        Co&& await_transform(Co&& co) { return static_cast<Co&&>(co); }
        // FIXME: maybe only await when there are children
        // SuspendGoalAwaiter await_transform(SuspendGoal) { return SuspendGoalAwaiter{!goal.waitees.empty()}; };
        SuspendGoalAwaiter await_transform(SuspendGoal) { return SuspendGoalAwaiter{true}; };
    };
    /**
     * The coroutine being currently executed.
     * You MUST update this when switching the coroutine being executed!
     * This is used both for memory management and to resume the last
     * coroutine executed.
     */
    std::optional<Co> top_co;

    virtual Co init() = 0;
    inline Co init_wrapper();

    Done amDone(ExitCode result, std::optional<Error> ex = {});

    virtual void cleanup() { }

    /**
     * Project a `BuildResult` with just the information that pertains
     * to the given request.
     *
     * In general, goals may be aliased between multiple requests, and
     * the stored `BuildResult` has information for the union of all
     * requests. We don't want to leak what the other request are for
     * sake of both privacy and determinism, and this "safe accessor"
     * ensures we don't.
     */
    BuildResult getBuildResult(const DerivedPath &) const;

    /**
     * Exception containing an error message, if any.
     */
    std::optional<Error> ex;

    Goal(Worker & worker, DerivedPath path)
        : worker(worker), top_co(init_wrapper())
    { }

    virtual ~Goal()
    {
        trace("goal destroyed");
    }

    void work();

    void addWaitee(GoalPtr waitee);

    virtual void waiteeDone(GoalPtr waitee, ExitCode result);

    virtual void handleChildOutput(Descriptor fd, std::string_view data)
    {
        abort();
    }

    virtual void handleEOF(Descriptor fd)
    {
        abort();
    }

    void trace(std::string_view s);

    std::string getName() const
    {
        return name;
    }

    /**
     * Callback in case of a timeout.  It should wake up its waiters,
     * get rid of any running child processes that are being monitored
     * by the worker (important!), etc.
     */
    virtual void timedOut(Error && ex) = 0;

    virtual std::string key() = 0;

    /**
     * @brief Hint for the scheduler, which concurrency limit applies.
     * @see JobCategory
     */
    virtual JobCategory jobCategory() const = 0;
};

void addToWeakGoals(WeakGoals & goals, GoalPtr p);

}

template<typename... ArgTypes>
struct std::coroutine_traits<nix::Goal::Co, ArgTypes...> {
    using promise_type = nix::Goal::promise_type;
};

nix::Goal::Co nix::Goal::init_wrapper() {
    co_return init();
}
