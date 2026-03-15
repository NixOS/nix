#pragma once
///@file

#include "nix/store/store-api.hh"
#include "nix/store/build/goal.hh"

namespace nix {

class Worker;

/**
 * A goal for getting the output path of a single derived output.
 *
 * This is used by `DerivationResolutionGoal` to get input derivation
 * output paths for resolving CA derivations.
 */
class DerivedOutputGoal : public Goal
{
    /**
     * The output deriving path we're trying to realise.
     * This can be nested (dynamic derivations).
     */
    SingleDerivedPath::Built id;

    BuildMode buildMode;

public:
    DerivedOutputGoal(const SingleDerivedPath::Built & id, Worker & worker, BuildMode buildMode);

    /**
     * The output path. Will be set once the goal succeeds.
     */
    std::optional<StorePath> outputPath;

    std::string key() override;

    JobCategory jobCategory() const override
    {
        return JobCategory::Administration;
    };

private:
    Co init();
};

} // namespace nix
