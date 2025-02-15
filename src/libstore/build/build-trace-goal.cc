#include "build-trace-goal.hh"
#include "finally.hh"
#include "worker.hh"
#include "substitution-goal.hh"
#include "callback.hh"
#include "util.hh"
#include "derivations.hh"
#include "derivation-resolution-goal.hh"

namespace nix {

BuildTraceGoal::BuildTraceGoal(const SingleDerivedPath::Built & id, Worker & worker)
    : Goal{worker, DerivedPath::Opaque{.path = StorePath::dummy}}
    , id{id}
{
    name = fmt("substitution of '%s'", id.to_string(worker.store));
    trace("created");
}

Goal::Co BuildTraceGoal::init()
{
    trace("init");

    DrvOutput id2{
        .drvPath = StorePath::dummy,
        .outputName = id.output,
    };

    // No `std::visit` with coroutines :(
    if (const auto * path = std::get_if<SingleDerivedPath::Opaque>(&*id.drvPath)) {
        // At least we know the drv path statically, can procede
        id2.drvPath = path->path;
    } else if (const auto * outputDeriving = std::get_if<SingleDerivedPath::Built>(&*id.drvPath)) {
        // Dynamic derivation case, need to resolve that first.

        auto g = worker.makeBuildTraceGoal(outputDeriving->drvPath, outputDeriving->output);

        addWaitee(upcast_goal(g));
        co_await Suspend{};

        if (nrFailed > 0) {
            debug("The output deriving path '%s' could not be resolved", outputDeriving->to_string(worker.store));
            co_return amDone(nrNoSubstituters > 0 || nrIncompleteClosure > 0 ? ecIncompleteClosure : ecFailed);
        }

        id2.drvPath = g->outputInfo->outPath;
    }

    /* If the derivation already exists, we’re done */
    if ((outputInfo = worker.store.queryRealisation(id2))) {
        co_return amDone(ecSuccess);
    }


    /**
     * Firstly, whether we know the status, secondly, what it is
     */
    std::optional<bool> drvIsResolved;

    /* If the derivation has statically-known output paths */
    if (worker.evalStore.isValidPath(id2.drvPath)) {
        auto drv = worker.evalStore.readDerivation(id2.drvPath);
        auto os = drv.outputsAndOptPaths(worker.store);
        /* Mark what we now know */
        drvIsResolved = {drv.inputDrvs.map.empty()};
        if (auto * p = get(os, id2.outputName)) {
            if (auto & outPath = p->second) {
                outputInfo = std::make_shared<UnkeyedRealisation>(*outPath);
                co_return amDone(ecSuccess);
            } else {
                /* Otherwise, not failure, just looking up build trace below. */
            }
        } else {
            debug(
                "Derivation '%s' does not have output '%s', impossible to find build trace key-value pair",
                worker.store.printStorePath(id2.drvPath),
                id2.outputName);
            co_return amDone(ecFailed);
        }
    }

    auto subs = settings.useSubstitutes ? getDefaultSubstituters() : std::list<ref<Store>>();

    bool substituterFailed = false;

    if (!drvIsResolved || *drvIsResolved) {
        /* Since derivation might be resolved --- isn't known to be
           not-resolved, it might have entries. So, let's try querying
           the substituters. */
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
                id2, {[outPipe(outPipe), promise(promise)](std::future<std::shared_ptr<const UnkeyedRealisation>> res) {
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

            std::shared_ptr<const UnkeyedRealisation> outputInfo;
            try {
                outputInfo = promise->get_future().get();
            } catch (std::exception & e) {
                printError(e.what());
                substituterFailed = true;
            }

            if (!outputInfo)
                continue;

            worker.store.registerDrvOutput({*outputInfo, id2});

            trace("finished");
            co_return amDone(ecSuccess);
        }
    }

    /* Derivation might not be resolved, let's try doing that */
    trace("trying resolving derivation in build-trace goal");

    auto g = worker.makeDerivationResolutionGoal(id2.drvPath);

    co_await Suspend{};

    if (nrFailed > 0) {
        /* None left.  Terminate this goal and let someone else deal
           with it. */
        debug(
            "derivation output '%s' is required, but there is no substituter that can provide it",
            id2.render(worker.store));

        if (substituterFailed) {
            worker.failedSubstitutions++;
            worker.updateProgress();
        }

        /* Hack: don't indicate failure if there were no substituters.
           In that case the calling derivation should just do a
           build. */
        co_return amDone(substituterFailed ? ecFailed : ecNoSubstituters);
    }

    /* This should be set if the goal succeeded */
    assert(g->drv);

    /* Try everything again, now with a resolved derivation */
    auto bt2 = worker.makeBuildTraceGoal(makeConstantStorePathRef(g->resolvedDrvPath), id2.outputName);

    addWaitee(bt2);
    co_await Suspend{};

    /* Set the build trace value as our own. Note the signure will not
       match our key since we're the unresolved derivation, but that's
       fine. We're not writing it to the DB; that's `bt2`' job. */
    if (bt2->outputInfo)
        outputInfo = bt2->outputInfo;

    co_return amDone(bt2->exitCode, bt2->ex);
}

std::string BuildTraceGoal::key()
{
    /* "a$" ensures substitution goals happen before derivation
       goals. */
    return "a$" + std::string(id.to_string(worker.store));
}

void BuildTraceGoal::handleEOF(Descriptor fd)
{
    worker.wakeUp(shared_from_this());
}

}
