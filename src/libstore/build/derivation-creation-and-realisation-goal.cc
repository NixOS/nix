#include "derivation-creation-and-realisation-goal.hh"
#include "worker.hh"

namespace nix {

DerivationCreationAndRealisationGoal::DerivationCreationAndRealisationGoal(
    ref<SingleDerivedPath> drvReq, const OutputsSpec & wantedOutputs, Worker & worker, BuildMode buildMode)
    : Goal(worker, DerivedPath::Built{.drvPath = drvReq, .outputs = wantedOutputs})
    , drvReq(drvReq)
    , wantedOutputs(wantedOutputs)
    , buildMode(buildMode)
{
    name =
        fmt("outer obtaining drv from '%s' and then building outputs %s",
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

DerivationCreationAndRealisationGoal::~DerivationCreationAndRealisationGoal() {}

static StorePath pathPartOfReq(const SingleDerivedPath & req)
{
    return std::visit(
        overloaded{
            [&](const SingleDerivedPath::Opaque & bo) { return bo.path; },
            [&](const SingleDerivedPath::Built & bfd) { return pathPartOfReq(*bfd.drvPath); },
        },
        req.raw());
}

std::string DerivationCreationAndRealisationGoal::key()
{
    /* Ensure that derivations get built in order of their name,
       i.e. a derivation named "aardvark" always comes before "baboon". And
       substitution goals and inner derivation goals always happen before
       derivation goals (due to "b$"). */
    return "c$" + std::string(pathPartOfReq(*drvReq).name()) + "$" + drvReq->to_string(worker.store);
}

void DerivationCreationAndRealisationGoal::timedOut(Error && ex) {}

void DerivationCreationAndRealisationGoal::addWantedOutputs(const OutputsSpec & outputs)
{
    /* If we already want all outputs, there is nothing to do. */
    auto newWanted = wantedOutputs.union_(outputs);
    bool needRestart = !newWanted.isSubsetOf(wantedOutputs);
    wantedOutputs = newWanted;

    if (!needRestart)
        return;

    if (!optDrvPath)
        // haven't started steps where the outputs matter yet
        return;
    worker.makeDerivationGoal(*optDrvPath, outputs, buildMode);
}

Goal::Co DerivationCreationAndRealisationGoal::init()
{
    trace("outer init");

    /* The first thing to do is to make sure that the derivation
       exists.  If it doesn't, it may be created through a
       substitute. */
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
        addWaitee(worker.makeGoal(DerivedPath::fromSingle(*drvReq)));
        co_await Suspend{};
    }

    trace("outer load and build derivation");

    if (nrFailed != 0) {
        co_return amDone(ecFailed, Error("cannot build missing derivation '%s'", drvReq->to_string(worker.store)));
    }

    StorePath drvPath = resolveDerivedPath(worker.store, *drvReq);
    /* Build this step! */
    concreteDrvGoal = worker.makeDerivationGoal(drvPath, wantedOutputs, buildMode);
    {
        auto g = upcast_goal(concreteDrvGoal);
        /* We will finish with it ourselves, as if we were the derivational goal. */
        g->preserveException = true;
    }
    optDrvPath = std::move(drvPath);
    addWaitee(upcast_goal(concreteDrvGoal));
    co_await Suspend{};

    trace("outer build done");

    buildResult = upcast_goal(concreteDrvGoal)
                      ->getBuildResult(DerivedPath::Built{
                          .drvPath = drvReq,
                          .outputs = wantedOutputs,
                      });

    auto g = upcast_goal(concreteDrvGoal);
    co_return amDone(g->exitCode, g->ex);
}

}
