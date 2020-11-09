#pragma once

#include "store-api.hh"
#include "goal.hh"
#include "drv-output-info.hh"

namespace nix {

class Worker;

// Substitution of a derivation output.
// This is done in three steps:
// 1. Fetch the output info from a substituter
// 2. Substitute the corresponding output path
// 3. Register the output info
class DrvOutputSubstitutionGoal : public Goal {
private:
    // The drv output we're trying to substitue
    DrvOutputId id;

    // The output info corresponding to the given output id.
    // Will be filled once we can get it.
    std::optional<DrvOutputInfo> outputInfo;

    /* The remaining substituters. */
    std::list<ref<Store>> subs;

    /* Whether a substituter failed. */
    bool substituterFailed = false;

public:
    DrvOutputSubstitutionGoal(const DrvOutputId& id, Worker & worker, RepairFlag repair = NoRepair, std::optional<ContentAddress> ca = std::nullopt);

    typedef void (DrvOutputSubstitutionGoal::*GoalState)();
    GoalState state;

    void init();
    void tryNext();
    void outPathValid();
    void finished();

    void timedOut(Error && ex) override { abort(); };

    string key() override;

    void work() override;

};

}
