#pragma once
///@file

#include "nix/store/store-api.hh"
#include "nix/store/build/goal.hh"
#include "nix/store/realisation.hh"

namespace nix {

class Worker;
class DrvOutputSubstitutionGoal;

/**
 * This is the "outermost" goal type relating to build trace lookups.
 *
 * It handles nested `SingleDerivedPath::Built` (dynamic derivations) by
 * recursively resolving the path before delegating to `DrvOutputSubstitutionGoal`.
 *
 * This is analogous to `DerivationTrampolineGoal` which handles nested paths
 * for derivation building.
 */
class BuildTraceTrampolineGoal : public Goal
{
    /**
     * The output deriving path we're trying to resolve.
     * This can be nested (dynamic derivations).
     */
    SingleDerivedPath::Built id;

public:
    BuildTraceTrampolineGoal(const SingleDerivedPath::Built & id, Worker & worker);

    /**
     * The realisation corresponding to the given output id.
     * Will be filled once we can get it.
     */
    std::shared_ptr<const UnkeyedRealisation> outputInfo;

    std::string key() override;

    JobCategory jobCategory() const override
    {
        return JobCategory::Administration;
    };

private:
    Co init();
};

} // namespace nix
