#include "drv-output-substitution-goal.hh"
#include "finally.hh"
#include "worker.hh"
#include "substitution-goal.hh"
#include "callback.hh"

namespace nix {

DrvOutputSubstitutionGoal::DrvOutputSubstitutionGoal(
    const DrvOutput & id,
    Worker & worker,
    RepairFlag repair,
    std::optional<ContentAddress> ca)
    : Goal(worker, DerivedPath::Opaque { StorePath::dummy })
    , id(id)
{
    state = &DrvOutputSubstitutionGoal::init;
    name = fmt("substitution of '%s'", id.to_string());
    trace("created");
}


void DrvOutputSubstitutionGoal::init()
{
    trace("init");

    /* If the derivation already exists, we’re done */
    if (worker.store.queryRealisation(id)) {
        amDone(ecSuccess);
        return;
    }

    subs = settings.useSubstitutes ? getDefaultSubstituters() : std::list<ref<Store>>();
    tryNext();
}

void DrvOutputSubstitutionGoal::tryNext()
{
    trace("trying next substituter");

    if (subs.size() == 0) {
        /* None left.  Terminate this goal and let someone else deal
           with it. */
        debug("derivation output '%s' is required, but there is no substituter that can provide it", id.to_string());

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

    sub = subs.front();
    subs.pop_front();

    // FIXME: Make async
    // outputInfo = sub->queryRealisation(id);
    outPipe.create();
    promise = decltype(promise)();

    sub->queryRealisation(
        id, { [&](std::future<std::shared_ptr<const Realisation>> res) {
            try {
                Finally updateStats([this]() { outPipe.writeSide.close(); });
                promise.set_value(res.get());
            } catch (...) {
                promise.set_exception(std::current_exception());
            }
        } });

    worker.childStarted(shared_from_this(), {outPipe.readSide.get()}, true, false);

    state = &DrvOutputSubstitutionGoal::realisationFetched;
}

void DrvOutputSubstitutionGoal::realisationFetched()
{
    worker.childTerminated(this);

    try {
        outputInfo = promise.get_future().get();
    } catch (std::exception & e) {
        printError(e.what());
        substituterFailed = true;
    }

    if (!outputInfo) {
        return tryNext();
    }

    for (const auto & [depId, depPath] : outputInfo->dependentRealisations) {
        if (depId != id) {
            if (auto localOutputInfo = worker.store.queryRealisation(depId);
                localOutputInfo && localOutputInfo->outPath != depPath) {
                warn(
                    "substituter '%s' has an incompatible realisation for '%s', ignoring.\n"
                    "Local:  %s\n"
                    "Remote: %s",
                    sub->getUri(),
                    depId.to_string(),
                    worker.store.printStorePath(localOutputInfo->outPath),
                    worker.store.printStorePath(depPath)
                );
                tryNext();
                return;
            }
            addWaitee(worker.makeDrvOutputSubstitutionGoal(depId));
        }
    }

    addWaitee(worker.makePathSubstitutionGoal(outputInfo->outPath));

    if (waitees.empty()) outPathValid();
    else state = &DrvOutputSubstitutionGoal::outPathValid;
}

void DrvOutputSubstitutionGoal::outPathValid()
{
    assert(outputInfo);
    trace("output path substituted");

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

std::string DrvOutputSubstitutionGoal::key()
{
    /* "a$" ensures substitution goals happen before derivation
       goals. */
    return "a$" + std::string(id.to_string());
}

void DrvOutputSubstitutionGoal::work()
{
    (this->*state)();
}

void DrvOutputSubstitutionGoal::handleEOF(int fd)
{
    if (fd == outPipe.readSide.get()) worker.wakeUp(shared_from_this());
}


}
