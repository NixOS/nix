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
 * Try to recursively obtain build trace key-value pairs in order to
 * resolve the given output deriving path.
 */
class BuildTraceGoal : public Goal
{

    /**
     * The output derivation path we're trying to reasolve.
     */
    SingleDerivedPath::Built id;

public:
    BuildTraceGoal(const SingleDerivedPath::Built & id, Worker & worker);

    /**
     * The realisation corresponding to the given output id.
     * Will be filled once we can get it.
     */
    std::shared_ptr<const UnkeyedRealisation> outputInfo;

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
