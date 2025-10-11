#pragma once
///@file

#include "nix/store/parsed-derivations.hh"
#include "nix/store/store-api.hh"
#include "nix/store/pathlocks.hh"
#include "nix/store/build/goal.hh"

namespace nix {

/**
 * This is the "outermost" goal type relating to derivations --- by that
 * we mean that this one calls all the others for a given derivation.
 *
 * This is a purely "administrative" goal type, which doesn't do any "real
 * work". See `DerivationGoal` for what we mean by such an administrative goal.
 *
 * # Rationale
 *
 * It exists to solve two problems:
 *
 * 1. We want to build a derivation we don't yet have.
 *
 *    Traditionally, that simply means we try to substitute the missing
 *    derivation; simple enough. However, with (currently experimental)
 *    dynamic derivations, derivations themselves can be the outputs of
 *    other derivations. That means the general case is that a
 *    `DerivationTrampolineGoal` needs to create *another*
 *    `DerivationTrampolineGoal` goal to realize the derivation it needs.
 *    That goal in turn might need to create a third
 *    `DerivationTrampolineGoal`, the induction down to a statically known
 *    derivation as the base case is arbitrary deep.
 *
 * 2. Only a subset of outputs is needed, but such subsets are discovered
 * dynamically.
 *
 *    Consider derivations:
 *
 *    - A has outputs x, y, and z
 *
 *    - B needs A^x,y
 *
 *    - C needs A^y,z and B's single output
 *
 *    With the current `Worker` architecture, we're first discover
 *    needing `A^y,z` and then discover needing `A^x,y`. Of course, we
 *    don't want to download `A^y` twice, either.
 *
 *    The way we handle sharing work for `A^y` is to have
 *    `DerivationGoal` just handle a single output, and do slightly more
 *    work (though it is just an "administrative" goal too), and
 *    `DerivationTrampolineGoal` handle sets of goals, but have it (once the
 *    derivation itself has been gotten) *just* create
 *    `DerivationGoal`s.
 *
 *    That means it is fine to create man `DerivationTrampolineGoal` with
 *    overlapping sets of outputs, because all the "real work" will be
 *    coordinated via `DerivationGoal`s, and sharing will be discovered.
 *
 * Both these problems *can* be solved by having just a more powerful
 * `DerivationGoal`, but that makes `DerivationGoal` more complex.
 * However the more complex `DerivationGoal` has these downsides:
 *
 * 1. It needs to cope with only sometimes knowing a `StorePath drvPath`
 * (as opposed to a more general `SingleDerivedPath drvPath` with will
 * be only resolved to a `StorePath` part way through the control flow).
 *
 * 2. It needs complicated "restarting logic" to cope with the set of
 * "wanted outputs" growing over time.
 *
 * (1) is not so bad, but (2) is quite scary, and has been a source of
 * bugs in the past. By splitting out `DerivationTrampolineGoal`, we
 * crucially avoid a need for (2), letting goal sharing rather than
 * ad-hoc retry mechanisms accomplish the deduplication we need. Solving
 * (1) is just a by-product and extra bonus of creating
 * `DerivationTrampolineGoal`.
 *
 * # Misc Notes
 *
 * If we already have the derivation (e.g. if the evaluator has created
 * the derivation locally and then instructed the store to build it), we
 * can skip the derivation-getting goal entirely as a small
 * optimization.
 */
struct DerivationTrampolineGoal : public Goal
{
    /**
     * How to obtain a store path of the derivation to build.
     */
    ref<const SingleDerivedPath> drvReq;

    /**
     * The specific outputs that we need to build.
     */
    OutputsSpec wantedOutputs;

    DerivationTrampolineGoal(
        ref<const SingleDerivedPath> drvReq,
        const OutputsSpec & wantedOutputs,
        Worker & worker,
        BuildMode buildMode = bmNormal);

    DerivationTrampolineGoal(
        const StorePath & drvPath,
        const OutputsSpec & wantedOutputs,
        const Derivation & drv,
        Worker & worker,
        BuildMode buildMode = bmNormal);

    virtual ~DerivationTrampolineGoal();

    void timedOut(Error && ex) override {}

    std::string key() override;

    JobCategory jobCategory() const override
    {
        return JobCategory::Administration;
    };

private:

    BuildMode buildMode;

    Co init();
    Co haveDerivation(StorePath drvPath, Derivation drv);

    /**
     * Shared between both constructors
     */
    void commonInit();
};

} // namespace nix
