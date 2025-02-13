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
    name = fmt("substitution of '%s'", id.render(worker.store));
    trace("created");
}


Goal::Co DrvOutputSubstitutionGoal::init()
{
    trace("init");

    /* If the derivation already exists, we’re done */
    if (worker.store.queryRealisation(id)) {
        co_return amDone(ecSuccess);
    }

    auto subs = settings.useSubstitutes ? getDefaultSubstituters() : std::list<ref<Store>>();

    bool substituterFailed = false;

    for (const auto & sub : subs) {
        trace("trying next substituter");

        /* The callback of the curl download below can outlive `this` (if
           some other error occurs), so it must not touch `this`. So put
           the shared state in a separate refcounted object. */
        auto outPipe = std::make_shared<MuxablePipe>();
    #ifndef _WIN32
        outPipe->create();
    #else
        outPipe->createAsyncPipe(worker.ioport.get());
    #endif

        auto promise = std::make_shared<std::promise<std::shared_ptr<const UnkeyedRealisation>>>();

        sub->queryRealisation(
            id,
            { [outPipe(outPipe), promise(promise)](std::future<std::shared_ptr<const UnkeyedRealisation>> res) {
                try {
                    Finally updateStats([&]() { outPipe->writeSide.close(); });
                    promise->set_value(res.get());
                } catch (...) {
                    promise->set_exception(std::current_exception());
                }
            } });

        worker.childStarted(shared_from_this(), {
    #ifndef _WIN32
            outPipe->readSide.get()
    #else
            &*outPipe
    #endif
        }, true, false);

        co_await Suspend{};

        worker.childTerminated(this);

        try {
            outputInfo = promise->get_future().get();
        } catch (std::exception & e) {
            printError(e.what());
            substituterFailed = true;
        }

        if (!outputInfo) continue;

        addWaitee(worker.makePathSubstitutionGoal(outputInfo->outPath));
        co_await Suspend{};

        trace("output path substituted");

        if (nrFailed > 0) {
            debug("The output path of the derivation output '%s' could not be substituted", id.render(worker.store));
            co_return amDone(nrNoSubstituters > 0 || nrIncompleteClosure > 0 ? ecIncompleteClosure : ecFailed);
        }

        worker.store.registerDrvOutput({*outputInfo, id});

        trace("finished");
        co_return amDone(ecSuccess);
    }

    /* None left.  Terminate this goal and let someone else deal
       with it. */
    debug("derivation output '%s' is required, but there is no substituter that can provide it", id.render(worker.store));

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
    /* "a$" ensures substitution goals happen before derivation
       goals. */
    return "a$" + std::string(id.render(worker.store));
}

void DrvOutputSubstitutionGoal::handleEOF(Descriptor fd)
{
    worker.wakeUp(shared_from_this());
}


}
