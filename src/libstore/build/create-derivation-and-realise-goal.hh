#pragma once

#include "parsed-derivations.hh"
#include "lock.hh"
#include "store-api.hh"
#include "pathlocks.hh"
#include "goal.hh"

namespace nix {

struct DerivationGoal;

/**
 * This goal type is essentially the serial composition (like function
 * composition) of a goal for getting a derivation, and then a
 * `DerivationGoal` using the newly-obtained derivation.
 *
 * In the (currently experimental) general inductive case of derivations
 * that are themselves build outputs, that first goal will be *another*
 * `CreateDerivationAndRealiseGoal`. In the (much more common) base-case
 * where the derivation has no provence and is just referred to by
 * (content-addressed) store path, that first goal is a
 * `SubstitutionGoal`.
 *
 * If we already have the derivation (e.g. if the evalutator has created
 * the derivation locally and then instructured the store to build it),
 * we can skip the first goal entirely as a small optimization.
 */
struct CreateDerivationAndRealiseGoal : public Goal
{
    /**
     * How to obtain a store path of the derivation to build.
     */
    ref<SingleDerivedPath> drvReq;

    /**
     * The path of the derivation, once obtained.
     **/
    std::optional<StorePath> optDrvPath;

    /**
     * The goal for the corresponding concrete derivation.
     **/
    std::shared_ptr<DerivationGoal> concreteDrvGoal;

    /**
     * The specific outputs that we need to build.
     */
    OutputsSpec wantedOutputs;

    typedef void (CreateDerivationAndRealiseGoal::*GoalState)();
    GoalState state;

    /**
     * The final output paths of the build.
     *
     * - For input-addressed derivations, always the precomputed paths
     *
     * - For content-addressed derivations, calcuated from whatever the
     *   hash ends up being. (Note that fixed outputs derivations that
     *   produce the "wrong" output still install that data under its
     *   true content-address.)
     */
    OutputPathMap finalOutputs;

    BuildMode buildMode;

    CreateDerivationAndRealiseGoal(ref<SingleDerivedPath> drvReq,
        const OutputsSpec & wantedOutputs, Worker & worker,
        BuildMode buildMode = bmNormal);
    virtual ~CreateDerivationAndRealiseGoal();

    void timedOut(Error && ex) override;

    std::string key() override;

    void work() override;

    /**
     * Add wanted outputs to an already existing derivation goal.
     */
    void addWantedOutputs(const OutputsSpec & outputs);

    /**
     * The states.
     */
    void getDerivation();
    void loadAndBuildDerivation();
    void buildDone();

    JobCategory jobCategory() const override {
        return JobCategory::Administration;
    };
};

}
