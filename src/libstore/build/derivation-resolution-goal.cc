#include "drv-output-substitution-goal.hh"
#include "finally.hh"
#include "worker.hh"
#include "substitution-goal.hh"
#include "callback.hh"

namespace nix {

DrvOutputSubstitutionGoal::DrvOutputSubstitutionGoal(
    const DrvOutput & id, Worker & worker, RepairFlag repair, std::optional<ContentAddress> ca)
    : Goal(worker, DerivedPath::Opaque{StorePath::dummy})
    , id(id)
{
    name = fmt("substitution of '%s'", id.render(worker.store));
    trace("created");
}

Goal::Co DrvOutputSubstitutionGoal::init()
{
    trace("init");

    addWaitee(upcast_goal(worker.makeBuildTraceGoal(makeConstantStorePathRef(id.drvPath), id.outputName)));
    co_await Suspend{};

    trace("output path substituted");

    if (nrFailed > 0) {
        debug("The output path of the derivation output '%s' could not be substituted", id.render(worker.store));
        co_return amDone(nrNoSubstituters > 0 || nrIncompleteClosure > 0 ? ecIncompleteClosure : ecFailed);
    }

    trace("finished");
    co_return amDone(ecSuccess);
}

std::string DrvOutputSubstitutionGoal::key()
{
    /* "a$" ensures substitution goals happen before derivation
       goals. */
    return "b$" + std::string(id.render(worker.store));
}

void DrvOutputSubstitutionGoal::handleEOF(Descriptor fd)
{
    worker.wakeUp(shared_from_this());
}

}
