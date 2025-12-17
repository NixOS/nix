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
 * Fetch a `Realisation` (drv ⨯ output name -> output path) from a
 * substituter.
 *
 * If the output store object itself should also be substituted, that is
 * the responsibility of the caller to do so.
 *
 * @todo rename this `BuidlTraceEntryGoal`, which will make sense
 * especially once `Realisation` is renamed to `BuildTraceEntry`.
 */
class DrvOutputSubstitutionGoal : public Goal
{

    /**
     * The drv output we're trying to substitute
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
