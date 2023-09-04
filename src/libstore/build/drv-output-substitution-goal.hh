#pragma once

#include "store-api.hh"
#include "goal.hh"
#include "realisation.hh"
#include <thread>
#include <future>

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
    DrvOutput id;

    // The realisation corresponding to the given output id.
    // Will be filled once we can get it.
    std::shared_ptr<const Realisation> outputInfo;

    /* The remaining substituters. */
    std::list<ref<Store>> subs;

    /* The current substituter. */
    std::shared_ptr<Store> sub;

    Pipe outPipe;
    std::thread thr;
    std::promise<std::shared_ptr<const Realisation>> promise;

    /* Whether a substituter failed. */
    bool substituterFailed = false;

public:
    DrvOutputSubstitutionGoal(const DrvOutput& id, Worker & worker, RepairFlag repair = NoRepair, std::optional<ContentAddress> ca = std::nullopt);

    typedef void (DrvOutputSubstitutionGoal::*GoalState)();
    GoalState state;

    void init();
    void tryNext();
    void realisationFetched();
    void outPathValid();
    void finished();

    void timedOut(Error && ex) override { abort(); };

    std::string key() override;

    void work() override;
    void handleEOF(int fd) override;
};

}
