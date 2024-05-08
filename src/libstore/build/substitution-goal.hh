#pragma once
///@file

#include "worker.hh"
#include "store-api.hh"
#include "goal.hh"
#include "muxable-pipe.hh"
#include <coroutine>
#include <future>

namespace nix {

struct PathSubstitutionGoal : public Goal
{
    /**
     * The store path that should be realised through a substitute.
     */
    StorePath storePath;

    /**
     * The path the substituter refers to the path as. This will be
     * different when the stores have different names.
     */
    std::optional<StorePath> subPath;

    /**
     * The remaining substituters.
     */
    std::list<ref<Store>> subs;

    /**
     * The current substituter.
     */
    std::shared_ptr<Store> sub;

    /**
     * Whether a substituter failed.
     */
    bool substituterFailed = false;

    /**
     * Path info returned by the substituter's query info operation.
     */
    std::shared_ptr<const ValidPathInfo> info;

    /**
     * Pipe for the substituter's standard output.
     */
    MuxablePipe outPipe;

    /**
     * The substituter thread.
     */
    std::thread thr;

    std::promise<void> promise;

    /**
     * Whether to try to repair a valid path.
     */
    RepairFlag repair;

    /**
     * Location where we're downloading the substitute.  Differs from
     * storePath when doing a repair.
     */
    Path destPath;

    std::unique_ptr<MaintainCount<uint64_t>> maintainExpectedSubstitutions,
        maintainRunningSubstitutions, maintainExpectedNar, maintainExpectedDownload;


    /*
     * Suspend our goal and wait until we get work()-ed again.
     */
    struct SuspendGoal {};
    struct SuspendGoalAwaiter {
        PathSubstitutionGoal& goal;
        explicit SuspendGoalAwaiter(PathSubstitutionGoal& goal) : goal(goal) {}
        bool await_ready() noexcept { return false; };
        void await_suspend(std::coroutine_handle<>) noexcept;
        void await_resume() noexcept {};
    };
    struct Done {};
    struct Co {
        struct promise_type;
        using handle_type = std::coroutine_handle<promise_type>;
        handle_type handle;
        explicit Co(handle_type handle) : handle(handle) {
            assert(handle);
        };
        Co(const Co&) = delete;
        Co &operator=(const Co&) = delete;
        Co &operator=(Co&&) = delete;
        Co(Co&& rhs);
        ~Co();

        struct promise_type {
            std::coroutine_handle<> continuation;
            PathSubstitutionGoal& goal;

            promise_type(PathSubstitutionGoal& goal) : goal(goal) {};

            struct final_awaiter {
                bool await_ready() noexcept { return false; };;
                std::coroutine_handle<> await_suspend(std::coroutine_handle<promise_type>) noexcept;
                void await_resume() noexcept { assert(false); };
            };
            Co get_return_object();
            std::suspend_always initial_suspend() { return {}; };
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

            inline Co&& await_transform(Co&& co) { return static_cast<Co&&>(co); }
            inline SuspendGoalAwaiter await_transform(SuspendGoal) { return SuspendGoalAwaiter(goal); };
        };

        bool await_ready() { return false; };
        std::coroutine_handle<> await_suspend(std::coroutine_handle<Co::promise_type> handle);
        void await_resume() {};
    };
    Co top_co;
    std::coroutine_handle<> cur_co;

    /**
     * Content address for recomputing store path
     */
    std::optional<ContentAddress> ca;

    Done done(
        ExitCode result,
        BuildResult::Status status,
        std::optional<std::string> errorMsg = {});

public:
    PathSubstitutionGoal(const StorePath & storePath, Worker & worker, RepairFlag repair = NoRepair, std::optional<ContentAddress> ca = std::nullopt);
    ~PathSubstitutionGoal();

    void timedOut(Error && ex) override { abort(); };

    /**
     * We prepend "a$" to the key name to ensure substitution goals
     * happen before derivation goals.
     */
    std::string key() override
    {
        return "a$" + std::string(storePath.name()) + "$" + worker.store.printStorePath(storePath);
    }

    void work() override;

    /**
     * The states.
     */
    Co init();
    Co tryNext();
    Co gotInfo();
    Co referencesValid();
    Co tryToRun();
    Co finished();

    /**
     * Callback used by the worker to write to the log.
     */
    void handleChildOutput(Descriptor fd, std::string_view data) override;
    void handleEOF(Descriptor fd) override;

    /* Called by destructor, can't be overridden */
    void cleanup() override final;

    JobCategory jobCategory() const override {
        return JobCategory::Substitution;
    };
};

}
