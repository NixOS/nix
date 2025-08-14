#pragma once
///@file

#include "nix/store/parsed-derivations.hh"
#include "nix/store/derivations.hh"
#include "nix/store/derivation-options.hh"
#include "nix/store/build/derivation-building-misc.hh"
#include "nix/store/outputs-spec.hh"
#include "nix/store/store-api.hh"
#include "nix/store/pathlocks.hh"
#include "nix/store/build/goal.hh"

namespace nix {

using std::map;

/** Used internally */
void runPostBuildHook(Store & store, Logger & logger, const StorePath & drvPath, const StorePathSet & outputPaths);

/**
 * A goal for realising a single output of a derivation. Various sorts of
 * fetching (which will be done by other goal types) is tried, and if none of
 * those succeed, the derivation is attempted to be built.
 *
 * This is a purely "administrative" goal type, which doesn't do any
 * "real work" of substituting (that would be `PathSubstitutionGoal` or
 * `DrvOutputSubstitutionGoal`) or building (that would be a
 * `DerivationBuildingGoal`). This goal type creates those types of
 * goals to attempt each way of realisation a derivation; they are tried
 * sequentially in order of preference.
 *
 * The derivation must already be gotten (in memory, in C++, parsed) and passed
 * to the caller. If the derivation itself needs to be gotten first, a
 * `DerivationTrampolineGoal` goal must be used instead.
 */
struct DerivationGoal : public Goal
{
    /** The path of the derivation. */
    StorePath drvPath;

    /**
     * The specific outputs that we need to build.
     */
    OutputName wantedOutput;

    DerivationGoal(
        const StorePath & drvPath,
        const Derivation & drv,
        const OutputName & wantedOutput,
        Worker & worker,
        BuildMode buildMode = bmNormal);
    ~DerivationGoal() = default;

    void timedOut(Error && ex) override
    {
        unreachable();
    };

    std::string key() override;

    JobCategory jobCategory() const override
    {
        return JobCategory::Administration;
    };

private:

    /**
     * The derivation stored at drvPath.
     */
    std::unique_ptr<Derivation> drv;

    /**
     * The remainder is state held during the build.
     */

    Hash outputHash;
    std::optional<InitialOutputStatus> outputKnown;

    BuildMode buildMode;

    std::unique_ptr<MaintainCount<uint64_t>> mcExpectedBuilds;

    /**
     * The states.
     */
    Co haveDerivation();

    /**
     * Update 'initialOutput' to determine the current status of the
     * outputs of the derivation. Also returns a Boolean denoting
     * whether all outputs are valid and non-corrupt, and a
     * 'SingleDrvOutputs' structure containing the valid outputs.
     */
    std::pair<bool, SingleDrvOutputs> checkPathValidity();

    /**
     * Aborts if any output is not valid or corrupt, and otherwise
     * returns a 'SingleDrvOutputs' structure containing all outputs.
     */
    SingleDrvOutputs assertPathValidity();

    Co repairClosure();

    Done done(BuildResult::Status status, SingleDrvOutputs builtOutputs = {}, std::optional<Error> ex = {});
};

} // namespace nix
