#include "nix/store/build/drv-output-substitution-goal.hh"
#include "nix/util/finally.hh"
#include "nix/store/build/worker.hh"
#include "nix/store/build/substitution-goal.hh"
#include "nix/util/callback.hh"
#include "nix/store/build/build-trace-goal.hh"

namespace nix {

DrvOutputSubstitutionGoal::DrvOutputSubstitutionGoal(
    const DrvOutput & id, Worker & worker, RepairFlag repair, std::optional<ContentAddress> ca)
    : Goal(worker, init())
    , id(id)
{
    name = fmt("substitution of '%s'", id.render(worker.store));
    trace("created");
}

Goal::Co DrvOutputSubstitutionGoal::init()
{
    trace("init");

    auto goal0 = worker.makeBuildTraceGoal({
        makeConstantStorePathRef(id.drvPath),
        id.outputName,
    });
    co_await await(Goals{upcast_goal(goal0)});

    trace("output path substituted");

    if (nrFailed > 0) {
        debug("The output path of the derivation output '%s' could not be substituted", id.render(worker.store));
        co_return amDone(nrNoSubstituters > 0 ? ecNoSubstituters : ecFailed);
    }

    auto goal1 = worker.makePathSubstitutionGoal(goal0->outputInfo->outPath);
    goal0.reset();

    goal1->preserveException = true;
    co_await await(Goals{upcast_goal(goal1)});
    co_return amDone(goal1->exitCode, goal1->ex);
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
