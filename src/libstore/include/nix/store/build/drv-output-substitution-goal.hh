#pragma once
///@file

#include <thread>
#include <future>

#include "nix/store/store-api.hh"
#include "nix/store/realisation.hh"
#include "nix/store/build/goal.hh"
#include "nix/util/muxable-pipe.hh"

namespace nix {

class Worker;

/**
 * Try to obtain build trace key-value pairs for a concrete derivation output.
 *
 * This goal takes a concrete `DrvOutput` (StorePath + output name) and queries
 * substituters for the realisation. For nested/dynamic derivations, use
 * `BuildTraceTrampolineGoal` which resolves the path first.
 *
 * @todo rename this `BuidlTraceEntryGoal`, which will make sense
 * especially once `Realisation` is renamed to `BuildTraceEntry`.
 */
class DrvOutputSubstitutionGoal : public Goal
{

    /**
     * The concrete derivation output we're trying to find.
     */
    DrvOutput id;

public:
    DrvOutputSubstitutionGoal(const DrvOutput & id, Worker & worker);

    /**
     * The realisation corresponding to the given output id.
     * Will be filled once we can get it.
     */
    std::shared_ptr<const UnkeyedRealisation> outputInfo;

    Co init();

    std::string key() override;

    JobCategory jobCategory() const override
    {
        return JobCategory::Substitution;
    };
};

} // namespace nix
