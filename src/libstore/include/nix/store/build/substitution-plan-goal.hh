#pragma once
///@file

#include "nix/store/build/goal.hh"
#include "nix/store/build/worker.hh"

namespace nix {

/**
 * Finds out where a store path could be substituted from: the first
 * substituter that has it with a trusted signature. Also plans the
 * substitution of the path's references, so that the plan for a path
 * covers its closure. Nothing is downloaded; `PathSubstitutionGoal`
 * does that, picking up from this plan.
 */
class SubstitutionPlanGoal : public Goal
{
    asio::awaitable<ExitCode> init();

public:
    const StorePath storePath;

    /**
     * Content address for recomputing the store path in substituters
     * with a different store directory.
     */
    const std::optional<ContentAddress> ca;

    /**
     * Where a path can be substituted from.
     */
    struct Candidate
    {
        ref<Store> sub;

        /**
         * The path the substituter refers to the path as. This will be
         * different when the stores have different names.
         */
        StorePath subPath;

        /**
         * Path info returned by the substituter's query info operation.
         */
        std::shared_ptr<const ValidPathInfo> info;
    };

    /**
     * The plan: set once the goal has finished successfully, unless the
     * path turned out to be valid already.
     */
    std::optional<Candidate> candidate;

    /**
     * The plans for the references of the path, if any.
     */
    Goals referencePlans;

    /**
     * Query the given substituters in order and return the first that
     * can provide the path, removing it and the ones before it from
     * `subs`. Errors from substituters are logged, except that the last
     * one is thrown if no substitute was found and `fallback` is not
     * set.
     */
    static asio::awaitable<std::optional<Candidate>> findSubstitute(
        Worker & worker,
        std::list<ref<Store>> & subs,
        const StorePath & storePath,
        const std::optional<ContentAddress> & ca);

    SubstitutionPlanGoal(const StorePath & storePath, Worker & worker, std::optional<ContentAddress> ca);

    ~SubstitutionPlanGoal();

    std::string key() override
    {
        return "ap$" + std::string(storePath.name()) + "$" + worker.store.printStorePath(storePath);
    }

    JobCategory jobCategory() const override
    {
        return JobCategory::Administration;
    }
};

} // namespace nix
