#include "nix/store/build/derivation-trampoline-goal.hh"
#include "nix/store/build/worker.hh"
#include "nix/store/derivations.hh"

namespace nix {

DerivationTrampolineGoal::DerivationTrampolineGoal(
    ref<const SingleDerivedPath> drvReq, const OutputsSpec & wantedOutputs, Worker & worker, BuildMode buildMode)
    : Goal(worker, init())
    , drvReq(drvReq)
    , wantedOutputs(wantedOutputs)
    , buildMode(buildMode)
{
    commonInit();
}

DerivationTrampolineGoal::DerivationTrampolineGoal(
    const StorePath & drvPath,
    const OutputsSpec & wantedOutputs,
    const Derivation & drv,
    Worker & worker,
    BuildMode buildMode)
    : Goal(worker, haveDerivation(drvPath, drv))
    , drvReq(makeConstantStorePathRef(drvPath))
    , wantedOutputs(wantedOutputs)
    , buildMode(buildMode)
{
    commonInit();
}

void DerivationTrampolineGoal::commonInit()
{
    name =
        fmt("obtaining derivation from '%s' and then building outputs %s",
            drvReq->to_string(worker.store),
            std::visit(
                overloaded{
                    [&](const OutputsSpec::All) -> std::string { return "* (all of them)"; },
                    [&](const OutputsSpec::Names os) { return concatStringsSep(", ", quoteStrings(os)); },
                },
                wantedOutputs.raw));
    trace("created outer");

    worker.updateProgress();
}

DerivationTrampolineGoal::~DerivationTrampolineGoal() {}

static StorePath pathPartOfReq(const SingleDerivedPath & req)
{
    return std::visit(
        overloaded{
            [&](const SingleDerivedPath::Opaque & bo) { return bo.path; },
            [&](const SingleDerivedPath::Built & bfd) { return pathPartOfReq(*bfd.drvPath); },
        },
        req.raw());
}

std::string DerivationTrampolineGoal::key()
{
    return "da$" + std::string(pathPartOfReq(*drvReq).name()) + "$" + DerivedPath::Built{
        .drvPath = drvReq,
        .outputs = wantedOutputs,
    }.to_string(worker.store);
}

Goal::Co DerivationTrampolineGoal::init()
{
    trace("need to load derivation from file");

    /* The first thing to do is to make sure that the derivation
       exists.  If it doesn't, it may be built from another derivation,
       or merely substituted. We can make goal to get it and not worry
       about which method it takes to get the derivation. */
    if (auto optDrvPath = [this]() -> std::optional<StorePath> {
            if (buildMode != bmNormal)
                return std::nullopt;

            auto drvPath = StorePath::dummy;
            try {
                drvPath = resolveDerivedPath(worker.store, *drvReq);
            } catch (MissingRealisation &) {
                return std::nullopt;
            }
            auto cond = worker.evalStore.isValidPath(drvPath) || worker.store.isValidPath(drvPath);
            return cond ? std::optional{drvPath} : std::nullopt;
        }()) {
        trace(
            fmt("already have drv '%s' for '%s', can go straight to building",
                worker.store.printStorePath(*optDrvPath),
                drvReq->to_string(worker.store)));
    } else {
        trace("need to obtain drv we want to build");
        Goals waitees{worker.makeGoal(DerivedPath::fromSingle(*drvReq))};
        co_await await(std::move(waitees));
    }

    trace("outer load and build derivation");

    if (nrFailed != 0) {
        co_return amDone(ecFailed, Error("cannot build missing derivation '%s'", drvReq->to_string(worker.store)));
    }

    StorePath drvPath = resolveDerivedPath(worker.store, *drvReq);

    /* `drvPath' should already be a root, but let's be on the safe
       side: if the user forgot to make it a root, we wouldn't want
       things being garbage collected while we're busy. */
    worker.evalStore.addTempRoot(drvPath);

    /* Get the derivation. It is probably in the eval store, but it might be in the main store:

         - Resolved derivation are resolved against main store realisations, and so must be stored there.

         - Dynamic derivations are built, and so are found in the main store.
     */
    auto drv = [&] {
        for (auto * drvStore : {&worker.evalStore, &worker.store})
            if (drvStore->isValidPath(drvPath))
                return drvStore->readDerivation(drvPath);
        assert(false);
    }();

    co_return haveDerivation(std::move(drvPath), std::move(drv));
}

Goal::Co DerivationTrampolineGoal::haveDerivation(StorePath drvPath, Derivation drv)
{
    trace("have derivation, will kick off derivations goals per wanted output");

    auto resolvedWantedOutputs = std::visit(
        overloaded{
            [&](const OutputsSpec::Names & names) -> OutputsSpec::Names { return names; },
            [&](const OutputsSpec::All &) -> OutputsSpec::Names {
                StringSet outputs;
                for (auto & [outputName, _] : drv.outputs)
                    outputs.insert(outputName);
                return outputs;
            },
        },
        wantedOutputs.raw);

    Goals concreteDrvGoals;

    /* Build this step! */

    for (auto & output : resolvedWantedOutputs) {
        auto g = upcast_goal(worker.makeDerivationGoal(drvPath, drv, output, buildMode, false));
        g->preserveException = true;
        /* We will finish with it ourselves, as if we were the derivational goal. */
        concreteDrvGoals.insert(std::move(g));
    }

    // Copy on purpose
    co_await await(Goals(concreteDrvGoals));

    trace("outer build done");

    auto & g = *concreteDrvGoals.begin();
    buildResult = g->buildResult;
    if (auto * successP = buildResult.tryGetSuccess())
        for (auto & g2 : concreteDrvGoals)
            if (auto * successP2 = g2->buildResult.tryGetSuccess())
                for (auto && [x, y] : successP2->builtOutputs)
                    successP->builtOutputs.insert_or_assign(x, y);

    co_return amDone(g->exitCode, g->ex);
}

} // namespace nix
