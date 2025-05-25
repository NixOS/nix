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
    DerivationResolutionGoal(const StorePath & storePath, Worker & worker);

    /**
     * The resolved derivation, if we succeeded.
     */
    std::shared_ptr<BasicDerivation> resolvedDrv;

    /**
     * The path to derivation above, if we succeeded.
     *
     * Garbage that should not be read otherwise.
     */
    StorePath resolvedDrvPath = StorePath::dummy;

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
