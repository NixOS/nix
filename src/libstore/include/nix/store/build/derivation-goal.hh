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
void runPostBuildHook(
    Store & store,
    Logger & logger,
    const StorePath & drvPath,
    const StorePathSet & outputPaths);

/**
 * A goal for building some or all of the outputs of a derivation.
 */
struct DerivationGoal : public Goal
{
    /** The path of the derivation. */
    StorePath drvPath;

    /**
     * The specific outputs that we need to build.
     */
    OutputsSpec wantedOutputs;

    /**
     * See `needRestart`; just for that field.
     */
    enum struct NeedRestartForMoreOutputs {
        /**
         * The goal state machine is progressing based on the current value of
         * `wantedOutputs. No actions are needed.
         */
        OutputsUnmodifedDontNeed,
        /**
         * `wantedOutputs` has been extended, but the state machine is
         * proceeding according to its old value, so we need to restart.
         */
        OutputsAddedDoNeed,
        /**
         * The goal state machine has progressed to the point of doing a build,
         * in which case all outputs will be produced, so extensions to
         * `wantedOutputs` no longer require a restart.
         */
        BuildInProgressWillNotNeed,
    };

    /**
     * Whether additional wanted outputs have been added.
     */
    NeedRestartForMoreOutputs needRestart = NeedRestartForMoreOutputs::OutputsUnmodifedDontNeed;

    /**
     * The derivation stored at drvPath.
     */
    std::unique_ptr<Derivation> drv;

    /**
     * The remainder is state held during the build.
     */

    std::map<std::string, InitialOutput> initialOutputs;

    BuildMode buildMode;

    std::unique_ptr<MaintainCount<uint64_t>> mcExpectedBuilds;

    DerivationGoal(const StorePath & drvPath,
        const OutputsSpec & wantedOutputs, Worker & worker,
        BuildMode buildMode = bmNormal);
    DerivationGoal(const StorePath & drvPath, const BasicDerivation & drv,
        const OutputsSpec & wantedOutputs, Worker & worker,
        BuildMode buildMode = bmNormal);
    ~DerivationGoal() = default;

    void timedOut(Error && ex) override { unreachable(); };

    std::string key() override;

    /**
     * Add wanted outputs to an already existing derivation goal.
     */
    void addWantedOutputs(const OutputsSpec & outputs);

    /**
     * The states.
     */
    Co loadDerivation();
    Co haveDerivation();

    /**
     * Wrappers around the corresponding Store methods that first consult the
     * derivation.  This is currently needed because when there is no drv file
     * there also is no DB entry.
     */
    std::map<std::string, std::optional<StorePath>> queryPartialDerivationOutputMap();
    OutputPathMap queryDerivationOutputMap();

    /**
     * Update 'initialOutputs' to determine the current status of the
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

    Done done(
        BuildResult::Status status,
        SingleDrvOutputs builtOutputs = {},
        std::optional<Error> ex = {});

    JobCategory jobCategory() const override {
        return JobCategory::Administration;
    };
};

}
