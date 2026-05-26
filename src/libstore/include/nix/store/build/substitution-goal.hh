#pragma once
///@file

#include "nix/store/build/worker.hh"
#include "nix/store/store-api.hh"
#include "nix/store/build/goal.hh"
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
     * The substituter thread.
     */
    std::thread thr;

    std::unique_ptr<MaintainCount<uint64_t>> maintainExpectedSubstitutions, maintainRunningSubstitutions,
        maintainExpectedNar, maintainExpectedDownload;

    /**
     * Content address for recomputing store path
     */
    std::optional<ContentAddress> ca;

    /**
     * Set when this path's substitute was found, but one of its references
     * could not be realised (i.e. the substituter served an incomplete
     * closure). Lets a downstream derivation goal retry substituting this
     * path after building its inputs to fill the hole.
     */
    bool incompleteClosure = false;

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

    /**
     * The states.
     */
    Co init();
    Co gotInfo();
    Co tryToRun(
        StorePath subPath, nix::ref<Store> sub, std::shared_ptr<const ValidPathInfo> info, bool & substituterFailed);
    Co finished();

    /* Called by destructor, can't be overridden */
    void cleanup() override final;

    JobCategory jobCategory() const override
    {
        return JobCategory::Substitution;
    };
};

} // namespace nix
