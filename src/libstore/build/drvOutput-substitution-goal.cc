#include "substitution-goal.hh"
#include "drv-output-info.hh"

namespace nix {
class DrvOutputSubstitutionGoal : public SubstitutionGoal
{
private:
    DrvOutputId id;

    /* DrvOutput info returned by the substituter */
    std::optional<DrvOutputInfo> drvOutputInfo;

    std::optional<GoalState> tryCurrent() override;
    void referencesValid() override;
    void tryToRun() override;

    DrvInput getTarget() const override { return id; }

public:
    DrvOutputSubstitutionGoal(const DrvOutputId&, Worker&, RepairFlag);
};

DrvOutputSubstitutionGoal::DrvOutputSubstitutionGoal(const DrvOutputId& id, Worker& worker, RepairFlag repair)
    : SubstitutionGoal(worker, repair)
    , id(id)
{
    name = fmt("substitution of '%s'", id.to_string());
    trace("created");
}


std::optional<DrvOutputSubstitutionGoal::GoalState> DrvOutputSubstitutionGoal::tryCurrent()
{
    // XXX Should be done in a separate thread
    drvOutputInfo = sub->queryDrvOutputInfo(id);
    if (!drvOutputInfo) {
        return std::nullopt;
    }
    remotelyKnownPath = drvOutputInfo->outPath;
    // XXX: Handle the case of a different storeDir between local and remote
    if (sub->storeDir == worker.store.storeDir)
        assert(!locallyKnownPath || remotelyKnownPath == *locallyKnownPath);
    else {
        return std::nullopt;
    }
    // XXX: Query for signatures

    /* To maintain the closure invariant, we first have to realise the
       drv outputs referenced by this one. */
    for (auto & i : drvOutputInfo->references)
        addWaitee(worker.makeSubstitutionGoal(i));

    return {&SubstitutionGoal::referencesValid};
}

void DrvOutputSubstitutionGoal::referencesValid()
{
    if (nrFailed > 0) {
        debug("some references of the drv output '%s' could not be realised", id.to_string());
        amDone(nrNoSubstituters > 0 || nrIncompleteClosure > 0 ? ecIncompleteClosure : ecFailed);
        return;
    }
    addWaitee(worker.makeSubstitutionGoal(drvOutputInfo->outPath, repair));
    state = &SubstitutionGoal::tryToRun;
}

void DrvOutputSubstitutionGoal::tryToRun()
{
    if (nrFailed > 0) {
        if (nrNoSubstituters != nrFailed) {
            substituterFailed = 1;
        }
        tryNext();
        return;
    }

    assert(worker.store.isValidPath(drvOutputInfo->outPath));
    promise = std::promise<void>();

    outPipe.create();

    worker.store.registerDrvOutput(id, *drvOutputInfo);
    promise.set_value();
    // thr = std::thread([&]() {
    //     worker.store.registerDrvOutput(id, *drvOutputInfo);
    //     promise.set_value();
    // });
    // worker.childStarted(shared_from_this(), {outPipe.readSide.get()}, true, false);
    // state = &SubstitutionGoal::finished;
    amDone(ecSuccess);
}

std::shared_ptr<SubstitutionGoal> makeSubstitutionGoal(const DrvOutputId& id, Worker & worker, RepairFlag repair, [[maybe_unused]] std::optional<ContentAddress> ca)
{
    return std::make_shared<DrvOutputSubstitutionGoal>(id, worker, repair);
}

}
