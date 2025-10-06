#pragma once
///@file

#include <thread>
#include <future>

#include "nix/store/store-api.hh"
#include "nix/store/build/goal.hh"
#include "nix/store/realisation.hh"
#include "nix/util/muxable-pipe.hh"

namespace nix {

class Worker;

/**
 * Substitution of a derivation output.
 * This is done in three steps:
 * 1. Fetch the output info from a substituter
 * 2. Substitute the corresponding output path
 * 3. Register the output info
 */
class DrvOutputSubstitutionGoal : public Goal
{

    /**
     * The drv output we're trying to substitute
     */
    DrvOutput id;

public:
    DrvOutputSubstitutionGoal(
        const DrvOutput & id,
        Worker & worker,
        RepairFlag repair = NoRepair,
        std::optional<ContentAddress> ca = std::nullopt);

    typedef void (DrvOutputSubstitutionGoal::*GoalState)();
    GoalState state;

    Co init();
    Co realisationFetched(Goals waitees, std::shared_ptr<const Realisation> outputInfo, nix::ref<nix::Store> sub);

    void timedOut(Error && ex) override
    {
        unreachable();
    };

    std::string key() override;

    void handleEOF(Descriptor fd) override;

    JobCategory jobCategory() const override
    {
        return JobCategory::Substitution;
    };
};

} // namespace nix
