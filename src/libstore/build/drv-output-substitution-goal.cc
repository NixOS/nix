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
    name = fmt("substitution of '%s'", id.to_string());
    trace("created");
}


Goal::Co DrvOutputSubstitutionGoal::init()
{
    trace("init");

    /* If the derivation already exists, we’re done */
    if (worker.store.queryRealisation(id)) {
        co_return amDone(ecSuccess);
    }

    subs = settings.useSubstitutes ? getDefaultSubstituters() : std::list<ref<Store>>();
    co_return tryNext();
}

Goal::Co DrvOutputSubstitutionGoal::tryNext()
{
    trace("trying next substituter");

    if (subs.size() == 0) {
        /* None left.  Terminate this goal and let someone else deal
           with it. */
        debug("derivation output '%s' is required, but there is no substituter that can provide it", id.to_string());

        if (substituterFailed) {
            worker.failedSubstitutions++;
            worker.updateProgress();
        }

        /* Hack: don't indicate failure if there were no substituters.
           In that case the calling derivation should just do a
           build. */
        co_return amDone(substituterFailed ? ecFailed : ecNoSubstituters);
    }

    sub = subs.front();
    subs.pop_front();

    // FIXME: Make async
    // outputInfo = sub->queryRealisation(id);

    /* The callback of the curl download below can outlive `this` (if
       some other error occurs), so it must not touch `this`. So put
       the shared state in a separate refcounted object. */
    downloadState = std::make_shared<DownloadState>();
#ifndef _WIN32
    downloadState->outPipe.create();
#else
    downloadState->outPipe.createAsyncPipe(worker.ioport.get());
#endif

    sub->queryRealisation(
        id,
        { [downloadState(downloadState)](std::future<std::shared_ptr<const Realisation>> res) {
            try {
                Finally updateStats([&]() { downloadState->outPipe.writeSide.close(); });
                downloadState->promise.set_value(res.get());
            } catch (...) {
                downloadState->promise.set_exception(std::current_exception());
            }
        } });

    worker.childStarted(shared_from_this(), {
#ifndef _WIN32
        downloadState->outPipe.readSide.get()
#else
        &downloadState->outPipe
#endif
    }, true, false);

    co_await SuspendGoal{};
    co_return realisationFetched();
}

Goal::Co DrvOutputSubstitutionGoal::realisationFetched()
{
    worker.childTerminated(this);

    try {
        outputInfo = downloadState->promise.get_future().get();
    } catch (std::exception & e) {
        printError(e.what());
        substituterFailed = true;
    }

    if (!outputInfo) {
        co_return tryNext();
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
                co_return tryNext();
            }
            addWaitee(worker.makeDrvOutputSubstitutionGoal(depId));
        }
    }

    addWaitee(worker.makePathSubstitutionGoal(outputInfo->outPath));

    if (!waitees.empty()) co_await SuspendGoal{};
    co_return outPathValid();
}

Goal::Co DrvOutputSubstitutionGoal::outPathValid()
{
    assert(outputInfo);
    trace("output path substituted");

    if (nrFailed > 0) {
        debug("The output path of the derivation output '%s' could not be substituted", id.to_string());
        co_return amDone(nrNoSubstituters > 0 || nrIncompleteClosure > 0 ? ecIncompleteClosure : ecFailed);
    }

    worker.store.registerDrvOutput(*outputInfo);
    co_return finished();
}

Goal::Co DrvOutputSubstitutionGoal::finished()
{
    trace("finished");
    co_return amDone(ecSuccess);
}

std::string DrvOutputSubstitutionGoal::key()
{
    /* "a$" ensures substitution goals happen before derivation
       goals. */
    return "a$" + std::string(id.to_string());
}

void DrvOutputSubstitutionGoal::handleEOF(Descriptor fd)
{
    if (fd == downloadState->outPipe.readSide.get()) worker.wakeUp(shared_from_this());
}


}
