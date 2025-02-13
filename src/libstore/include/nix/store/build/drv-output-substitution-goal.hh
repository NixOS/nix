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

    /**
     * The realisation corresponding to the given output id.
     * Will be filled once we can get it.
     */
    std::shared_ptr<const UnkeyedRealisation> outputInfo;

    Co init();

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

}
