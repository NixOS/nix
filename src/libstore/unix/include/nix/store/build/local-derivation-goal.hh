#pragma once
///@file

#include "nix/store/build/derivation-goal.hh"

namespace nix {

/**
 * Create a local derivation goal, see `DerivationGoal` for info on each
 * constructor variant.
 */
std::shared_ptr<DerivationGoal> makeLocalDerivationGoal(
    const StorePath & drvPath, const OutputsSpec & wantedOutputs, Worker & worker, BuildMode buildMode = bmNormal);

/**
 * Create a local derivation goal, see `DerivationGoal` for info on each
 * constructor variant.
 */
std::shared_ptr<DerivationGoal> makeLocalDerivationGoal(
    const StorePath & drvPath,
    const BasicDerivation & drv,
    const OutputsSpec & wantedOutputs,
    Worker & worker,
    BuildMode buildMode = bmNormal);

}
