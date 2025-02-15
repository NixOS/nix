#pragma once
///@file

#include <thread>
#include <future>

#include "store-api.hh"
#include "goal.hh"
#include "realisation.hh"
#include "muxable-pipe.hh"

namespace nix {

class Worker;

/**
 * The purpose of this is to resolve the given derivation, so that it
 * only has constant deriving paths as inputs.
 */
class DerivationResolutionGoal : public Goal
{

    /**
     * The derivation we're trying to substitute
     */
    StorePath drvPath;

public:
    DerivationResolutionGoal(
        const DrvOutput & id,
        Worker & worker,
        RepairFlag repair = NoRepair,
        std::optional<ContentAddress> ca = std::nullopt);

    /**
     * The resolved derivation, if we succeeded.
     */
    std::shared_ptr<BasicDerivation> drv;

    /**
     * The path to derivation above, if we succeeded.
     *
     * Garbage that should not be read otherwise.
     */
    StorePath resolvedDrvPath = StorePath::dummy;

    Co init() override;

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
