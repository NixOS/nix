#pragma once
///@file

#include "nix/store/store-api.hh"
#include "nix/store/build-result.hh"

#include <coroutine>
#include <queue>
#include <variant>

namespace nix {

class TimedOut final : public CloneableError<TimedOut, BuildError>
{
    void anchor() override;

public:
    time_t maxDuration;

    TimedOut(time_t maxDuration);
};

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
    /* VTable anchor to avoid weak linkage of the vtable - it breaks
       dynamic_cast across shared libraries on Darwin. */
    virtual void anchor();
public:
    /**
     * Event types for child process communication, delivered via coroutines.
     */
    struct ChildOutput
    {
        Descriptor fd;
        std::string data;
    };

    struct ChildEOF
    {
        Descriptor fd;
    };

    using ChildEvent = std::variant<ChildOutput, ChildEOF, std::unique_ptr<TimedOut>>;

private:
    class ChildEvents
    {
        /**
         * Structured queue of child events:
         * - outputs: stream of data from child
         * - eof: optional end-of-stream marker
         * - timeout: optional timeout that flushes/overrides other events
         */
        std::queue<ChildOutput> childOutputs;
        std::optional<ChildEOF> childEOF;
        std::unique_ptr<TimedOut> childTimeout;

    public:
        void pushChildEvent(ChildOutput event);
        void pushChildEvent(ChildEOF event);
        void pushChildEvent(TimedOut event);
        bool hasChildEvent() const;
        ChildEvent popChildEvent();
    };

    /**
     * Goals that this goal is waiting for.
     */
    Goals waitees;

    /**
     * Memoised result of key().
     */
    std::optional<std::string> cachedKey;

    ChildEvents childEvents;

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

    /**
     * Tag type for `co_await`-ing child events.
     * Returns a `ChildEvent` when resumed.
     */
    struct WaitForChildEvent
    {};

    template<typename T>
    class AwaitableFrame;
    class AwaitableFrameBase;

    /**
     * Handle to coroutine using @ref Co and @ref promise_type.
     */
    template<typename T>
    using HandleType = std::coroutine_handle<AwaitableFrame<T>>;
    using HandleTypeBase = std::coroutine_handle<AwaitableFrameBase>;

    template<typename T = void>
    struct BasicCo;

    using Co = BasicCo<void>;

    class CoBase
    {
    protected:
        friend struct Goal;

        /**
         * The underlying handle.
         */
        std::coroutine_handle<> handle;

        explicit CoBase(std::coroutine_handle<> h) noexcept
            : handle(h)
        {
        }

    public:
        CoBase() noexcept
            : handle(nullptr)
        {
        }

        CoBase(CoBase && rhs) noexcept
            : handle(std::exchange(rhs.handle, nullptr))
        {
        }

        CoBase & operator=(CoBase && rhs) noexcept;
        CoBase(const CoBase &) = delete;
        CoBase & operator=(const CoBase &) = delete;
        ~CoBase();
    };

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
     */
    template<typename T>
    struct [[nodiscard]] BasicCo : CoBase
    {
        BasicCo() noexcept = default;

        explicit BasicCo(HandleType<T> h) noexcept
            : CoBase(h)
        {
        }

        AwaitableFrame<T> & frame() const
        {
            assert(handle);
            return HandleType<T>::from_address(handle.address()).promise();
        }
    };

    static_assert(sizeof(BasicCo<void>) == sizeof(CoBase));

    template<typename T>
    struct AsyncCallback
    {
        fun<void(Callback<T>)> fn;
    };

    class AwaitableFrameBase
    {
        friend struct Goal;

    protected:
        /**
         * Either this is who called us, or it is who we will tail-call.
         * It is what we "jump" to once we are done.
         */
        std::optional<CoBase> continuation;

        void * resultSlot = nullptr;
        void (*moveResult)(AwaitableFrameBase * from, void * to) noexcept = nullptr;

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

    private:
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
            HandleTypeBase handle;

            bool await_ready()
            {
                return false;
            };

            template<class Promise>
            void await_suspend(std::coroutine_handle<Promise> h)
            {
                handle = HandleTypeBase::from_address(h.address());
            }

            void await_resume()
            {
                assert(handle);
                assert(handle.promise().goal);                           // goal must be set
                assert(handle.promise().goal->top_co);                   // top_co of goal must be set
                assert(handle.promise().goal->top_co->handle == handle); // top_co of goal must be us
            }
        };

        template<typename T, typename Derived>
        class CoAwaiterBase
        {
            CoAwaiterBase() = default;

            CoAwaiterBase(BasicCo<T> c)
                : co(std::move(c))
            {
            }

        public:
            BasicCo<T> co;

            bool await_ready() const noexcept
            {
                return false;
            }

            template<class Promise>
            std::coroutine_handle<> await_suspend(std::coroutine_handle<Promise> caller)
            {
                auto & promise = co.frame();
                auto goal = caller.promise().goal;
                assert(goal);
                assert(!promise.continuation); // we must have no continuation
                assert(!promise.goal);         // we must not have a goal yet
                promise.goal = goal;
                promise.continuation = std::move(goal->top_co); // we set our continuation to be top_co (i.e. caller)
                if constexpr (!std::is_void_v<T>) {
                    promise.resultSlot = &static_cast<Derived *>(this)->result;
                }
                goal->top_co = std::move(co); // we set top_co to ourselves, don't use this anymore after this!
                return goal->top_co->handle;  // we execute ourselves
            }

            friend Derived;
        };

        template<typename T>
        struct CoAwaiter : CoAwaiterBase<T, CoAwaiter<T>>
        {
            CoAwaiter() = default;

            explicit CoAwaiter(BasicCo<T> co)
                : CoAwaiterBase<T, CoAwaiter<T>>(std::move(co))
            {
            }

            std::optional<T> result;

            T await_resume()
            {
                assert(result);
                return std::move(*result);
            }
        };

        /**
         * Awaiter for child events. Suspends and returns the
         * pending child event when resumed.
         */
        struct ChildEventAwaiter
        {
            HandleTypeBase handle;

            bool await_ready()
            {
                return handle && handle.promise().goal->childEvents.hasChildEvent();
            }

            template<class Promise>
            void await_suspend(std::coroutine_handle<Promise> h)
            {
                handle = HandleTypeBase::from_address(h.address());
            }

            ChildEvent await_resume()
            {
                assert(handle);
                return handle.promise().goal->childEvents.popChildEvent();
            }
        };

        /**
         * The awaiter used by @ref final_suspend.
         */
        struct FinalAwaiter
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
            template<class Promise>
            std::coroutine_handle<> await_suspend(std::coroutine_handle<Promise> h) noexcept;

            void await_resume() noexcept
            {
                assert(false);
            };
        };

        /**
         * Awaiter for @ref Suspend. Always suspends, but asserts
         * there are no pending child events (those should be
         * consumed first via @ref WaitForChildEvent).
         */
        struct SuspendAwaiter
        {
            AwaitableFrameBase & promise;

            bool await_ready()
            {
                assert(!promise.goal->childEvents.hasChildEvent());
                return false;
            }

            template<class Promise>
            void await_suspend([[maybe_unused]] std::coroutine_handle<Promise> h)
            {
            }

            void await_resume() {}
        };

    public:
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
         * Called on `co_return`. Creates @ref FinalAwaiter which
         * either jumps to continuation or suspends goal.
         */
        FinalAwaiter final_suspend() noexcept
        {
            return {};
        };

        /**
         * If an exception is thrown inside a coroutine,
         * we re-throw it in the context of the "resumer" of the continuation.
         */
        void unhandled_exception()
        {
            throw;
        };

        template<typename T>
        CoAwaiter<T> await_transform(BasicCo<T> && co)
        {
            return CoAwaiter<T>{std::move(co)};
        }

        template<typename T>
        auto await_transform(AsyncCallback<T> && acb);

        /**
         * Allows awaiting a @ref Suspend.
         * Always suspends.
         */
        SuspendAwaiter await_transform(Suspend)
        {
            return SuspendAwaiter{*this};
        };

        /**
         * Allows awaiting child events (output, EOF, timeout).
         */
        ChildEventAwaiter await_transform(WaitForChildEvent)
        {
            return ChildEventAwaiter{HandleTypeBase::from_promise(*this)};
        };
    };

    /**
     * Promise type for coroutines defined using @ref Co.
     * Attached to coroutine handle.
     *
     * @see boost::asio::detail::awaitable_frame for a reference implementation
     * of a similar pattern.
     */
    template<typename T>
    class AwaitableFrame : public AwaitableFrameBase
    {
        /**
         * The return value.
         */
        std::optional<T> result;

        static void doMoveResult(AwaitableFrameBase * from, void * to) noexcept
        {
            auto * self = static_cast<AwaitableFrame *>(from);
            auto * slot = static_cast<std::optional<T> *>(to);
            slot->emplace(std::move(*self->result));
        }

    public:
        /**
         * Called by compiler generated code to construct the `Co`
         * that is returned from a `Co`-returning coroutine.
         */
        BasicCo<T> get_return_object()
        {
            return BasicCo<T>{HandleType<T>::from_promise(*this)};
        }

        /**
         * Does nothing, but provides an opportunity for
         * @ref final_suspend to happen.
         */
        void return_value(Done &&) {}

        template<typename R>
        void return_value(R && r)
        {
            result.emplace(std::forward<R>(r));
            moveResult = &doMoveResult;
        }
    };

protected:
    /**
     * The coroutine being currently executed.
     * MUST be updated when switching the coroutine being executed.
     * This is used both for memory management and to resume the last
     * coroutine executed.
     * Destroying this should destroy all coroutines created for this goal.
     */
    std::optional<CoBase> top_co;

    /**
     * Signals that the goal is done.
     * `co_return` the result. If you're not inside a coroutine, you can ignore
     * the return value safely.
     *
     * Prefer using `doneSuccess` or `doneFailure` instead, which ensure
     * `buildResult` is set correctly.
     */
    Done amDone(ExitCode result);

    /**
     * Signals successful completion of the goal.
     * Sets `buildResult` and calls `amDone`.
     */
    Done doneSuccess(BuildResult::Success success);

    /**
     * Signals failed completion of the goal.
     * Sets `buildResult` and calls `amDone`.
     *
     * @param result The exit code (ecFailed or ecNoSubstituters)
     * @param failure The failure details including status and error message
     */
    Done doneFailure(ExitCode result, BuildResult::Failure failure);

public:
    virtual void cleanup() {}

    /**
     * Hack to say that this goal should not log the failure, but instead keep
     * it around. Set by a waitee which sees itself as the designated
     * continuation of this goal, responsible for reporting its
     * successes or failures.
     *
     * @todo this is yet another not-nice hack in the goal system that
     * we ought to get rid of. See #11927
     */
    bool preserveFailure = false;

    Goal(Worker & worker, Co init);

    virtual ~Goal()
    {
        trace("goal destroyed");
    }

    void work();

    /**
     * Called by the worker when data is received from a child process.
     * Stores the event and resumes the coroutine.
     */
    void handleChildOutput(Descriptor fd, std::string_view data);

    /**
     * Called by the worker when EOF is received from a child process.
     * Stores the event and resumes the coroutine.
     */
    void handleEOF(Descriptor fd);

    /**
     * Called by the worker when a build times out.
     * Stores the event and resumes the coroutine.
     */
    void timedOut(TimedOut && ex);

    void trace(std::string_view s);

    std::string getName() const
    {
        return name;
    }

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
     * Memoising variant of key(). We really don't want to pay the overhead of
     * allocating strings just to compare Goals.
     */
    std::string_view keyCached() &
    {
        if (cachedKey)
            return *cachedKey;
        return *(cachedKey = key());
    }

    /**
     * @brief Hint for the scheduler, which concurrency limit applies.
     * @see JobCategory
     */
    virtual JobCategory jobCategory() const = 0;

protected:
    Co await(Goals waitees);

    /**
     * Awaiting on the resulting coroutine yields the goal for several seconds.
     * Used for retrying goals blocked on acquiring lockfiles.
     */
    Co waitForAWhile();

    /**
     * Awaiting on the resulting coroutine yields the goal until it is
     * explicitly woken up via Worker::wakeUp. Wakeup can be queued from another
     * thread via Worker::Waker.
     */
    Co waitUntilWoken();

    Co waitForBuildSlot();
    Co yield();
};

void addToWeakGoals(WeakGoals & goals, GoalPtr p);

template<typename Promise>
std::coroutine_handle<> Goal::AwaitableFrameBase::FinalAwaiter::await_suspend(std::coroutine_handle<Promise> h) noexcept
{
    auto & p = h.promise();
    auto goal = p.goal;
    assert(goal);
    goal->trace("in FinalAwaiter");
    auto c = std::move(p.continuation);

    if (p.resultSlot && p.moveResult)
        p.moveResult(&p, p.resultSlot);

    if (c) {
        // We still have a continuation, i.e. work to do.
        // We assert that the goal is still busy.
        assert(goal->exitCode == ecBusy);
        assert(goal->top_co);              // Goal must have an active coroutine.
        assert(goal->top_co->handle == h); // The active coroutine must be us.
        assert(p.alive);                   // We must not have been destructed.

        // we move continuation to the top,
        // note: previous top_co is actually h, so by moving into it,
        // we're calling the destructor on h, DON'T use h and p after this!

        // We move our continuation into `top_co`, i.e. the marker for the active continuation.
        // By doing this we destruct the old `top_co`, i.e. us, so `h` can't be used anymore.
        // Be careful not to access freed memory!
        goal->top_co = std::move(c);

        // We resume `top_co`.
        return goal->top_co->handle;
    } else {
        // We have no continuation, i.e. no more work to do,
        // so the goal must not be busy anymore.
        assert(goal->exitCode != ecBusy);

        // We reset `top_co` for good measure.
        p.goal->top_co = {};

        // We jump to the noop coroutine, which doesn't do anything and immediately suspends.
        // This passes control back to the caller of goal.work().
        return std::noop_coroutine();
    }
}

template<>
struct Goal::AwaitableFrameBase::CoAwaiter<void> : CoAwaiterBase<void, CoAwaiter<void>>
{
    CoAwaiter() = default;

    explicit CoAwaiter(BasicCo<void> co)
        : CoAwaiterBase<void, CoAwaiter<void>>(std::move(co))
    {
    }

    void await_resume() {}
};

template<>
struct Goal::AwaitableFrame<void> : Goal::AwaitableFrameBase
{
    Co get_return_object()
    {
        return Co{HandleType<void>::from_promise(*this)};
    }

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
     * @ref final_suspend is called after this, and @ref FinalAwaiter will
     * pass control off to @ref continuation.
     *
     * If we already have a continuation, that continuation is set as
     * the continuation of the new continuation. Thus, the continuation
     * passed to @ref return_value must not have a continuation set.
     */
    void return_value(Co &&);
};

} // namespace nix

template<typename T, typename... ArgTypes>
struct std::coroutine_traits<nix::Goal::BasicCo<T>, ArgTypes...>
{
    using promise_type = nix::Goal::AwaitableFrame<T>;
};
