#include "goal-impl.hh"

#include "nix/store/build/drv-output-substitution-goal.hh"
#include "nix/store/build/worker.hh"
#include "nix/util/callback.hh"

namespace nix {

DrvOutputSubstitutionGoal::DrvOutputSubstitutionGoal(const DrvOutput & id, Worker & worker)
    : Goal(worker, init())
    , id(id)
{
    name = fmt("substitution of '%s'", id.render(worker.store));
    trace("created");
}

Goal::Co DrvOutputSubstitutionGoal::init()
{
    trace("init");

    /* If the derivation already exists, we’re done */
    if ((outputInfo = worker.store.queryRealisation(id))) {
        co_return amDone(ecSuccess);
    }

    auto subs = worker.getSubstituters();

    bool substituterFailed = false;

    for (const auto & sub : subs) {
        trace("trying next substituter");

        try {
            outputInfo = co_await AsyncCallback<std::shared_ptr<const UnkeyedRealisation>>(
                [sub, id = this->id](auto cb) { sub->queryRealisation(id, std::move(cb)); });
        } catch (std::exception & e) {
            printError(e.what());
            substituterFailed = true;
        }

        if (!outputInfo)
            continue;

        trace("finished");
        co_return amDone(ecSuccess);
    }

    /* None left.  Terminate this goal and let someone else deal
       with it. */
    debug(
        "derivation output '%s' is required, but there is no substituter that can provide it", id.render(worker.store));

    if (substituterFailed) {
        worker.failedSubstitutions++;
        worker.updateProgress();
    }

    /* Hack: don't indicate failure if there were no substituters.
       In that case the calling derivation should just do a
       build. */
    co_return amDone(substituterFailed ? ecFailed : ecNoSubstituters);
}

std::string DrvOutputSubstitutionGoal::key()
{
    return "a$" + std::string(id.render(worker.store));
}

} // namespace nix
