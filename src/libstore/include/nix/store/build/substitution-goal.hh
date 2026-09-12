#pragma once
///@file

#include "nix/store/build/worker.hh"
#include "nix/store/store-api.hh"
#include "nix/store/build/goal.hh"
#include <coroutine>
#include <future>
#include <source_location>

namespace nix {

class PathSubstitutionGoal : public Goal
{
    /**
     * The store path that should be realised through a substitute.
     */
    StorePath storePath;

    /**
     * Whether to try to repair a valid path.
     */
    RepairFlag repair;

    std::unique_ptr<MaintainCount<uint64_t>> maintainExpectedSubstitutions, maintainRunningSubstitutions,
        maintainExpectedNar, maintainExpectedDownload;

    /**
     * Content address for recomputing store path
     */
    std::optional<ContentAddress> ca;

    enum class SubstitutionResult {
        SubstituterFailed,
        SubstituteGone,
        Ok,
    };

    using Result = std::pair<ExitCode, BuildResult>;

    asio::awaitable<Result> substitute();
    asio::awaitable<SubstitutionResult>
    tryToRun(StorePath subPath, nix::ref<Store> sub, std::shared_ptr<const ValidPathInfo> info);

public:
    PathSubstitutionGoal(
        const StorePath & storePath,
        Worker & worker,
        RepairFlag repair = NoRepair,
        std::optional<ContentAddress> ca = std::nullopt);

    ~PathSubstitutionGoal();

    std::string key() override
    {
        return "a$" + std::string(storePath.name()) + "$" + worker.store.printStorePath(storePath);
    }

    const StorePath & getStorePath() const &
    {
        return storePath;
    }

    /**
     * The states.
     */
    asio::awaitable<ExitCode> init();

    JobCategory jobCategory() const override
    {
        return JobCategory::Substitution;
    };
};

} // namespace nix
