#include "nix/store/build/build-trace-trampoline-goal.hh"
#include "nix/store/build/drv-output-substitution-goal.hh"
#include "nix/store/build/derivation-resolution-goal.hh"
#include "nix/store/build/worker.hh"
#include "nix/store/derivations.hh"
#include "nix/util/util.hh"

namespace nix {

BuildTraceTrampolineGoal::BuildTraceTrampolineGoal(const SingleDerivedPath::Built & id, Worker & worker)
    : Goal{worker, init()}
    , id{id}
{
    name = fmt("resolving build trace for '%s'", id.to_string(worker.store));
    trace("created");
}

static StorePath pathPartOfReq(const SingleDerivedPath & req)
{
    return std::visit(
        overloaded{
            [&](const SingleDerivedPath::Opaque & bo) { return bo.path; },
            [&](const SingleDerivedPath::Built & bfd) { return pathPartOfReq(*bfd.drvPath); },
        },
        req.raw());
}

std::string BuildTraceTrampolineGoal::key()
{
    return "bt$" + std::string(pathPartOfReq(*id.drvPath).name()) + "$" + id.to_string(worker.store);
}

Goal::Co BuildTraceTrampolineGoal::init()
{
    trace("init");

    DrvOutput id2{
        .drvPath = StorePath::dummy,
        .outputName = id.output,
    };

    // No `std::visit` with coroutines :(
    if (const auto * path = std::get_if<SingleDerivedPath::Opaque>(&*id.drvPath)) {
        // At least we know the drv path statically, can proceed
        id2.drvPath = path->path;
    } else if (const auto * outputDeriving = std::get_if<SingleDerivedPath::Built>(&*id.drvPath)) {
        // Dynamic derivation case, need to resolve that first.
        trace("need to resolve dynamic derivation first");

        auto g = worker.makeBuildTraceTrampolineGoal({
            outputDeriving->drvPath,
            outputDeriving->output,
        });

        co_await await(Goals{upcast_goal(g)});

        if (nrFailed > 0) {
            co_return amDone(nrNoSubstituters > 0 ? ecNoSubstituters : ecFailed);
        }

        id2.drvPath = g->outputInfo->outPath;
    }

    trace("have concrete drv path");

    /* If the realisation already exists, we're done */
    if ((outputInfo = worker.store.queryRealisation(id2))) {
        co_return amDone(ecSuccess);
    }

    /**
     * Firstly, whether we know the status, secondly, what it is
     */
    std::optional<bool> drvIsResolved;

    std::optional<Derivation> drvOpt;

    if (worker.evalStore.isValidPath(id2.drvPath)) {
        drvOpt = worker.evalStore.readDerivation(id2.drvPath);
    } else if (worker.store.isValidPath(id2.drvPath)) {
        drvOpt = worker.store.readDerivation(id2.drvPath);
    }

    /* If we have the derivation, and the derivation has statically-known output paths */
    if (drvOpt) {
        auto & drv = *drvOpt;
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
            co_return amDone(ecFailed);
        }
    }

    bool substituterFailed = false;

    if (!drvIsResolved || *drvIsResolved) {
        /* Since derivation might be resolved --- isn't known to be
           not-resolved, it might have entries. So, let's try querying
           the substituters. */
        auto g = worker.makeDrvOutputSubstitutionGoal(id2);

        co_await await(Goals{upcast_goal(g)});

        if (g->exitCode == ecSuccess) {
            outputInfo = g->outputInfo;
            co_return amDone(ecSuccess);
        }

        if (g->exitCode == ecFailed) {
            substituterFailed = true;
        }

        /* If ecNoSubstituters or ecFailed, fall through to try resolution */
    }

    if (drvIsResolved && *drvIsResolved) {
        /* Derivation is already resolved, no point trying to resolve it */
        co_return amDone(substituterFailed ? ecFailed : ecNoSubstituters);
    }

    /* Derivation might not be resolved, let's try doing that */
    trace("trying resolving derivation in build-trace goal");

    if (!drvOpt) {
        /* Derivation not available locally, can't try resolution.
           Let the caller fall back to building. */
        co_return amDone(substituterFailed ? ecFailed : ecNoSubstituters);
    }

    auto g = worker.makeDerivationResolutionGoal(id2.drvPath, *drvOpt, bmNormal);

    co_await await(Goals{g});

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
    assert(g->resolvedDrv);

    if (g->resolvedDrv->first != id2.drvPath) {
        /* Try again with the resolved derivation. Since we know it's
           resolved, we can go straight to DrvOutputSubstitutionGoal. */
        DrvOutput convergentId{g->resolvedDrv->first, id2.outputName};

        auto bt2 = worker.makeDrvOutputSubstitutionGoal(convergentId);

        /* No longer need 'g' */
        g = nullptr;

        co_await await(Goals{upcast_goal(bt2)});

        /* Set the build trace value as our own. Note the signature will not
           match our key since we're the unresolved derivation, but that's
           fine. We're not writing it to the DB; that's `bt2`'s job. */
        if (bt2->outputInfo)
            outputInfo = bt2->outputInfo;

        co_return amDone(bt2->exitCode);
    } else {
        /* None left.  Terminate this goal and let someone else deal
           with it. */
        debug("build trace is not known for '%s', derivation is already resolved", id2.to_string());

        if (substituterFailed) {
            worker.failedSubstitutions++;
            worker.updateProgress();
        }

        /* Hack: don't indicate failure if there were no substituters.
           In that case the calling derivation should just do a
           build. */
        co_return amDone(substituterFailed ? ecFailed : ecNoSubstituters);
    }
}

} // namespace nix
