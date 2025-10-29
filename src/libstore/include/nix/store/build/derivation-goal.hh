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

    /**
     * @param storeDerivation See `DerivationBuildingGoal`. This is just passed along.
     */
    DerivationGoal(
        const StorePath & drvPath,
        const Derivation & drv,
        const OutputName & wantedOutput,
        Worker & worker,
        BuildMode buildMode,
        bool storeDerivation);
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

    const Hash outputHash;

    const BuildMode buildMode;

    /**
     * The remainder is state held during the build.
     */

    std::unique_ptr<MaintainCount<uint64_t>> mcExpectedBuilds;

    /**
     * The states.
     */
    Co haveDerivation(bool storeDerivation);

    /**
     * Return `std::nullopt` if the output is unknown, e.g. un unbuilt
     * floating content-addressing derivation. Otherwise, returns a pair
     * of a `Realisation`, containing among other things the store path
     * of the wanted output, and a `PathStatus` with the
     * current status of that output.
     */
    std::optional<std::pair<UnkeyedRealisation, PathStatus>> checkPathValidity();

    /**
     * Aborts if any output is not valid or corrupt, and otherwise
     * returns a 'Realisation' for the wanted output.
     */
    UnkeyedRealisation assertPathValidity();

    Co repairClosure();

    Done doneSuccess(BuildResult::Success::Status status, UnkeyedRealisation builtOutput);

    Done doneFailure(BuildError ex);
};

} // namespace nix
