#pragma once
///@file

#include "nix/store/store-api.hh"
#include "nix/store/build-result.hh"

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

struct CompareGoalPtrs
{
    bool operator()(const GoalPtr & a, const GoalPtr & b) const;
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
    /**
     * A goal that does no "real" work by itself, and just exists to depend on
     * other goals which *do* do real work. These goals therefore are not
     * limited.
     *
     * These goals cannot infinitely create themselves, so there is no risk of
     * a "fork bomb" type situation (which would be a problem even though the
     * goal do no real work) either.
     */
    Administration,
};

struct Goal : public std::enable_shared_from_this<Goal>
{
private:
    /**
     * Goals that this goal is waiting for.
     */
    Goals waitees;

public:
    typedef enum { ecBusy, ecSuccess, ecFailed, ecNoSubstituters } ExitCode;

    /**
     * Backlink to the worker.
     */
    Worker & worker;

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
     * Name of this goal for debugging purposes.
     */
    std::string name;

    /**
     * Whether the goal is finished.
     */
    ExitCode exitCode = ecBusy;

    /**
     * Build result.
     */
    BuildResult buildResult;

    /**
     * Suspend our goal and wait until we get `work`-ed again.
     * `co_await`-able by @ref Co.
     */
    struct Suspend
    {};

    /**
     * Return from the current coroutine and suspend our goal
     * if we're not busy anymore, or jump to the next coroutine
     * set to be executed/resumed.
     */
    struct Return
    {};

    /**
     * `co_return`-ing this will end the goal.
     * If you're not inside a coroutine, you can safely discard this.
     */
    struct [[nodiscard]] Done
    {
    private:
        Done() {}

        friend Goal;
    };

    // forward declaration of promise_type, see below
    struct promise_type;

    /**
     * Handle to coroutine using @ref Co and @ref promise_type.
     */
    using handle_type = std::coroutine_handle<promise_type>;

    /**
     * C++20 coroutine wrapper for use in goal logic.
     * Coroutines are functions that use `co_await`/`co_return` (and `co_yield`, but not supported by @ref Co).
     *
     * @ref Co is meant to be used by methods of subclasses of @ref Goal.
     * The main functionality provided by `Co` is
     * - `co_await Suspend{}`: Suspends the goal.
     * - `co_await f()`: Waits until `f()` finishes.
     * - `co_return f()`: Tail-calls `f()`.
     * - `co_return Return{}`: Ends coroutine.
     *
     * The idea is that you implement the goal logic using coroutines,
     * and do the core thing a goal can do, suspension, when you have
     * children you're waiting for.
     * Coroutines allow you to resume the work cleanly.
     *
     * @note Brief explanation of C++20 coroutines:
     *       When you `Co f()`, a `std::coroutine_handle<promise_type>` is created,
     *       alongside its @ref promise_type.
     *       There are suspension points at the beginning of the coroutine,
     *       at every `co_await`, and at the final (possibly implicit) `co_return`.
     *       Once suspended, you can resume the `std::coroutine_handle` by doing `coroutine_handle.resume()`.
     *       Suspension points are implemented by passing a struct to the compiler
     *       that implements `await_sus`pend.
     *       `await_suspend` can either say "cancel suspension", in which case execution resumes,
     *       "suspend", in which case control is passed back to the caller of `coroutine_handle.resume()`
     *       or the place where the coroutine function is initially executed in the case of the initial
     *       suspension, or `await_suspend` can specify another coroutine to jump to, which is
     *       how tail calls are implemented.
     *
     * @note Resources:
     *       - https://lewissbaker.github.io/
     *       - https://www.chiark.greenend.org.uk/~sgtatham/quasiblog/coroutines-c++20/
     *       - https://www.scs.stanford.edu/~dm/blog/c++-coroutines.html
     *
     * @todo Allocate explicitly on stack since HALO thing doesn't really work,
     *       specifically, there's no way to uphold the requirements when trying to do
     *       tail-calls without using a trampoline AFAICT.
     *
     * @todo Support returning data natively
     */
    struct [[nodiscard]] Co
    {
        /**
         * The underlying handle.
         */
        handle_type handle;

        explicit Co(handle_type handle)
            : handle(handle) {};
        void operator=(Co &&);
        Co(Co && rhs);
        ~Co();

        bool await_ready()
        {
            return false;
        };

        /**
         * When we `co_await` another `Co`-returning coroutine,
         * we tell the caller of `caller_coroutine.resume()` to switch to our coroutine (@ref handle).
         * To make sure we return to the original coroutine, we set it as the continuation of our
         * coroutine. In @ref promise_type::final_awaiter we check if it's set and if so we return to it.
         *
         * To explain in more understandable terms:
         * When we `co_await Co_returning_function()`, this function is called on the resultant @ref Co of
         * the _called_ function, and C++ automatically passes the caller in.
         *
         * `goal` field of @ref promise_type is also set here by copying it from the caller.
         */
        std::coroutine_handle<> await_suspend(handle_type handle);
        void await_resume() {};
    };

    /**
     * Used on initial suspend, does the same as `std::suspend_always`,
     * but asserts that everything has been set correctly.
     */
    struct InitialSuspend
    {
        /**
         * Handle of coroutine that does the
         * initial suspend
         */
        handle_type handle;

        bool await_ready()
        {
            return false;
        };

        void await_suspend(handle_type handle_)
        {
            handle = handle_;
        }

        void await_resume()
        {
            assert(handle);
            assert(handle.promise().goal);                           // goal must be set
            assert(handle.promise().goal->top_co);                   // top_co of goal must be set
            assert(handle.promise().goal->top_co->handle == handle); // top_co of goal must be us
        }
    };

    /**
     * Promise type for coroutines defined using @ref Co.
     * Attached to coroutine handle.
     */
    struct promise_type
    {
        /**
         * Either this is who called us, or it is who we will tail-call.
         * It is what we "jump" to once we are done.
         */
        std::optional<Co> continuation;

        /**
         * The goal that we're a part of.
         * Set either in @ref Co::await_suspend or in constructor of @ref Goal.
         */
        Goal * goal = nullptr;

        /**
         * Is set to false when destructed to ensure we don't use a
         * destructed coroutine by accident
         */
        bool alive = true;

        /**
         * The awaiter used by @ref final_suspend.
         */
        struct final_awaiter
        {
            bool await_ready() noexcept
            {
                return false;
            };

            /**
             * Here we execute our continuation, by passing it back to the caller.
             * C++ compiler will create code that takes that and executes it promptly.
             * `h` is the handle for the coroutine that is finishing execution,
             * thus it must be destroyed.
             */
            std::coroutine_handle<> await_suspend(handle_type h) noexcept;

            void await_resume() noexcept
            {
                assert(false);
            };
        };

        /**
         * Called by compiler generated code to construct the `Co`
         * that is returned from a `Co`-returning coroutine.
         */
        Co get_return_object();

        /**
         * Called by compiler generated code before body of coroutine.
         * We use this opportunity to set the @ref goal field
         * and `top_co` field of @ref Goal.
         */
        InitialSuspend initial_suspend()
        {
            return {};
        };

        /**
         * Called on `co_return`. Creates @ref final_awaiter which
         * either jumps to continuation or suspends goal.
         */
        final_awaiter final_suspend() noexcept
        {
            return {};
        };

        /**
         * Does nothing, but provides an opportunity for
         * @ref final_suspend to happen.
         */
        void return_value(Return) {}

        /**
         * Does nothing, but provides an opportunity for
         * @ref final_suspend to happen.
         */
        void return_value(Done) {}

        /**
         * When "returning" another coroutine, what happens is that
         * we set it as our own continuation, thus once the final suspend
         * happens, we transfer control to it.
         * The original continuation we had is set as the continuation
         * of the coroutine passed in.
         * @ref final_suspend is called after this, and @ref final_awaiter will
         * pass control off to @ref continuation.
         *
         * If we already have a continuation, that continuation is set as
         * the continuation of the new continuation. Thus, the continuation
         * passed to @ref return_value must not have a continuation set.
         */
        void return_value(Co &&);

        /**
         * If an exception is thrown inside a coroutine,
         * we re-throw it in the context of the "resumer" of the continuation.
         */
        void unhandled_exception()
        {
            throw;
        };

        /**
         * Allows awaiting a @ref Co.
         */
        Co && await_transform(Co && co)
        {
            return static_cast<Co &&>(co);
        }

        /**
         * Allows awaiting a @ref Suspend.
         * Always suspends.
         */
        std::suspend_always await_transform(Suspend)
        {
            return {};
        };
    };

protected:
    /**
     * The coroutine being currently executed.
     * MUST be updated when switching the coroutine being executed.
     * This is used both for memory management and to resume the last
     * coroutine executed.
     * Destroying this should destroy all coroutines created for this goal.
     */
    std::optional<Co> top_co;

    /**
     * Signals that the goal is done.
     * `co_return` the result. If you're not inside a coroutine, you can ignore
     * the return value safely.
     */
    Done amDone(ExitCode result, std::optional<Error> ex = {});

public:
    virtual void cleanup() {}

    /**
     * Hack to say that this goal should not log `ex`, but instead keep
     * it around. Set by a waitee which sees itself as the designated
     * continuation of this goal, responsible for reporting its
     * successes or failures.
     *
     * @todo this is yet another not-nice hack in the goal system that
     * we ought to get rid of. See #11927
     */
    bool preserveException = false;

    /**
     * Exception containing an error message, if any.
     */
    std::optional<Error> ex;

    Goal(Worker & worker, Co init)
        : worker(worker)
        , top_co(std::move(init))
    {
        // top_co shouldn't have a goal already, should be nullptr.
        assert(!top_co->handle.promise().goal);
        // we set it such that top_co can pass it down to its subcoroutines.
        top_co->handle.promise().goal = this;
    }

    virtual ~Goal()
    {
        trace("goal destroyed");
    }

    void work();

    virtual void handleChildOutput(Descriptor fd, std::string_view data)
    {
        unreachable();
    }

    virtual void handleEOF(Descriptor fd)
    {
        unreachable();
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

    /**
     * Used for comparisons. The order matters a bit for scheduling. We
     * want:
     *
     * 1. Substitution
     * 2. Derivation administrativia
     * 3. Actual building
     *
     * Also, ensure that derivations get processed in order of their
     * name, i.e. a derivation named "aardvark" always comes before
     * "baboon".
     */
    virtual std::string key() = 0;

    /**
     * @brief Hint for the scheduler, which concurrency limit applies.
     * @see JobCategory
     */
    virtual JobCategory jobCategory() const = 0;

protected:
    Co await(Goals waitees);

    Co waitForAWhile();
    Co waitForBuildSlot();
    Co yield();
};

void addToWeakGoals(WeakGoals & goals, GoalPtr p);

} // namespace nix

template<typename... ArgTypes>
struct std::coroutine_traits<nix::Goal::Co, ArgTypes...>
{
    using promise_type = nix::Goal::promise_type;
};
