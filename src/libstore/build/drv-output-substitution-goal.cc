#include "nix/store/build/drv-output-substitution-goal.hh"
#include "nix/util/finally.hh"
#include "nix/store/build/worker.hh"
#include "nix/store/build/substitution-goal.hh"
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
            id, {[outPipe(outPipe), promise(promise)](std::future<std::shared_ptr<const UnkeyedRealisation>> res) {
                try {
                    Finally updateStats([&]() { outPipe->writeSide.close(); });
                    promise->set_value(res.get());
                } catch (...) {
                    promise->set_exception(std::current_exception());
                }
            }});

        worker.childStarted(
            shared_from_this(),
            {
#ifndef _WIN32
                outPipe->readSide.get()
#else
                &*outPipe
#endif
            },
            true,
            false);

        while (true) {
            auto event = co_await WaitForChildEvent{};
            if (std::get_if<ChildOutput>(&event)) {
                // Doesn't process child output
            } else if (std::get_if<ChildEOF>(&event)) {
                break;
            } else if (std::get_if<TimedOut>(&event)) {
                unreachable();
            }
        }

        worker.childTerminated(this);

        try {
            outputInfo = promise->get_future().get();
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
