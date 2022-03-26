#include "drv-output-substitution-goal.hh"
#include "worker.hh"
#include "substitution-goal.hh"

namespace nix {

DrvOutputSubstitutionGoal::DrvOutputSubstitutionGoal(const DrvOutput& id, Worker & worker, RepairFlag repair, std::optional<ContentAddress> ca)
    : Goal(worker)
    , id(id)
{
    state = &DrvOutputSubstitutionGoal::init;
    name = fmt("substitution of '%s'", id.to_string());
    trace("created");
}


void DrvOutputSubstitutionGoal::init()
{
    trace("init");
    subs = settings.useSubstitutes ? getDefaultSubstituters() : std::list<ref<Store>>();
    tryNext();
}

void DrvOutputSubstitutionGoal::tryNext()
{
    trace("Trying next substituter");

    if (subs.size() == 0) {
        /* None left.  Terminate this goal and let someone else deal
           with it. */
        debug("drv output '%s' is required, but there is no substituter that can provide it", id.to_string());

        /* Hack: don't indicate failure if there were no substituters.
           In that case the calling derivation should just do a
           build. */
        amDone(substituterFailed ? ecFailed : ecNoSubstituters);

        if (substituterFailed) {
            worker.failedSubstitutions++;
            worker.updateProgress();
        }

        return;
    }

    auto sub = subs.front();
    subs.pop_front();

    // FIXME: Make async
    outputInfo = sub->queryRealisation(id);
    if (!outputInfo) {
        tryNext();
        return;
    }

    addWaitee(worker.makePathSubstitutionGoal(outputInfo->outPath));

    if (waitees.empty()) outPathValid();
    else state = &DrvOutputSubstitutionGoal::outPathValid;
}

void DrvOutputSubstitutionGoal::outPathValid()
{
    assert(outputInfo);
    trace("Output path substituted");

    if (nrFailed > 0) {
        debug("The output path of the derivation output '%s' could not be substituted", id.to_string());
        amDone(nrNoSubstituters > 0 || nrIncompleteClosure > 0 ? ecIncompleteClosure : ecFailed);
        return;
    }

    worker.store.registerDrvOutput(*outputInfo);
    finished();
}

void DrvOutputSubstitutionGoal::finished()
{
    trace("finished");
    amDone(ecSuccess);
}

string DrvOutputSubstitutionGoal::key()
{
    /* "a$" ensures substitution goals happen before derivation
       goals. */
    return "a$" + std::string(id.to_string());
}

void DrvOutputSubstitutionGoal::work()
{
    (this->*state)();
}

}
