#pragma once
///@file

#include "nix/store/build/worker.hh"
#include "nix/store/store-api.hh"
#include "nix/store/build/goal.hh"
#include "nix/util/muxable-pipe.hh"
#include <coroutine>
#include <future>
#include <source_location>

namespace nix {

struct PathSubstitutionGoal : public Goal
{
    /**
     * The store path that should be realised through a substitute.
     */
    StorePath storePath;

    /**
     * Whether to try to repair a valid path.
     */
    RepairFlag repair;

    /**
     * Pipe for the substituter's standard output.
     */
    MuxablePipe outPipe;

    /**
     * The substituter thread.
     */
    std::thread thr;

    std::unique_ptr<MaintainCount<uint64_t>> maintainExpectedSubstitutions, maintainRunningSubstitutions,
        maintainExpectedNar, maintainExpectedDownload;

    /**
     * Content address for recomputing store path
     */
    std::optional<ContentAddress> ca;

    Done doneSuccess(BuildResult::Success::Status status);

    Done doneFailure(ExitCode result, BuildResult::Failure::Status status, std::string errorMsg);

public:
    PathSubstitutionGoal(
        const StorePath & storePath,
        Worker & worker,
        RepairFlag repair = NoRepair,
        std::optional<ContentAddress> ca = std::nullopt);
    ~PathSubstitutionGoal();

    void timedOut(Error && ex) override
    {
        unreachable();
    };

    std::string key() override
    {
        return "a$" + std::string(storePath.name()) + "$" + worker.store.printStorePath(storePath);
    }

    /**
     * The states.
     */
    Co init();
    Co gotInfo();
    Co tryToRun(
        StorePath subPath, nix::ref<Store> sub, std::shared_ptr<const ValidPathInfo> info, bool & substituterFailed);
    Co finished();

    /**
     * Callback used by the worker to write to the log.
     */
    void handleChildOutput(Descriptor fd, std::string_view data) override {};
    void handleEOF(Descriptor fd) override;

    /* Called by destructor, can't be overridden */
    void cleanup() override final;

    JobCategory jobCategory() const override
    {
        return JobCategory::Substitution;
    };
};

} // namespace nix
