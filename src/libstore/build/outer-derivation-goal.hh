#pragma once

#include "parsed-derivations.hh"
#include "lock.hh"
#include "store-api.hh"
#include "pathlocks.hh"
#include "goal.hh"

namespace nix {

struct OuterDerivationGoal : public Goal
{
    /* How to obtain a store path of the derivation to build. */
    std::shared_ptr<SingleDerivedPath> drvReq;

    /* The path of the derivation, once obtained. */
    std::optional<StorePath> optDrvPath;

    /* The specific outputs that we need to build.  Empty means all of
       them. */
    StringSet wantedOutputs;

    typedef void (OuterDerivationGoal::*GoalState)();
    GoalState state;

    /* The final output paths of the build.

       - For input-addressed derivations, always the precomputed paths

       - For content-addressed derivations, calcuated from whatever the hash
         ends up being. (Note that fixed outputs derivations that produce the
         "wrong" output still install that data under its true content-address.)
     */
    OutputPathMap finalOutputs;

    BuildMode buildMode;

    OuterDerivationGoal(std::shared_ptr<SingleDerivedPath> drvReq,
        const StringSet & wantedOutputs, Worker & worker,
        BuildMode buildMode = bmNormal);
    virtual ~OuterDerivationGoal();

    void timedOut(Error && ex) override;

    string key() override;

    void work() override;

    /* Add wanted outputs to an already existing derivation goal. */
    void addWantedOutputs(const StringSet & outputs);

    /* The states. */
    void getDerivation();
    void loadAndBuildDerivation();
    void buildDone();
};

}
