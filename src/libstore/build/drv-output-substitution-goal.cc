#include "nix/store/build/drv-output-substitution-goal.hh"
#include "nix/util/finally.hh"
#include "nix/store/build/worker.hh"
#include "nix/store/build/substitution-goal.hh"
#include "nix/util/callback.hh"
#include "nix/store/store-open.hh"
#include "nix/store/globals.hh"

namespace nix {

DrvOutputSubstitutionGoal::DrvOutputSubstitutionGoal(const DrvOutput & id, Worker & worker)
    : Goal(worker, init())
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

        co_await Suspend{};

        worker.childTerminated(this);

        /*
         * The realisation corresponding to the given output id.
         * Will be filled once we can get it.
         */
        std::shared_ptr<const UnkeyedRealisation> outputInfo;

        try {
            outputInfo = promise->get_future().get();
        } catch (std::exception & e) {
            printError(e.what());
            substituterFailed = true;
        }

        if (!outputInfo)
            continue;

        bool failed = false;

        Goals waitees;

        for (const auto & [depId, depPath] : outputInfo->dependentRealisations) {
            if (depId != id) {
                if (auto localOutputInfo = worker.store.queryRealisation(depId);
                    localOutputInfo && localOutputInfo->outPath != depPath) {
                    warn(
                        "substituter '%s' has an incompatible realisation for '%s', ignoring.\n"
                        "Local:  %s\n"
                        "Remote: %s",
                        sub->config.getHumanReadableURI(),
                        depId.to_string(),
                        worker.store.printStorePath(localOutputInfo->outPath),
                        worker.store.printStorePath(depPath));
                    failed = true;
                    break;
                }
                waitees.insert(worker.makeDrvOutputSubstitutionGoal(depId));
            }
        }

        if (failed)
            continue;

        waitees.insert(worker.makePathSubstitutionGoal(outputInfo->outPath));

        co_await await(std::move(waitees));

        trace("output path substituted");

        if (nrFailed > 0) {
            debug("The output path of the derivation output '%s' could not be substituted", id.to_string());
            co_return amDone(nrNoSubstituters > 0 ? ecNoSubstituters : ecFailed);
        }

        worker.store.registerDrvOutput({*outputInfo, id});

        trace("finished");
        co_return amDone(ecSuccess);
    }

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

std::string DrvOutputSubstitutionGoal::key()
{
    return "a$" + std::string(id.to_string());
}

void DrvOutputSubstitutionGoal::handleEOF(Descriptor fd)
{
    worker.wakeUp(shared_from_this());
}

} // namespace nix
