#include "outer-derivation-goal.hh"
#include "worker.hh"

namespace nix {

OuterDerivationGoal::OuterDerivationGoal(std::shared_ptr<SingleDerivedPath> drvReq,
    const StringSet & wantedOutputs, Worker & worker, BuildMode buildMode)
    : Goal(worker)
    , drvReq(drvReq)
    , wantedOutputs(wantedOutputs)
    , buildMode(buildMode)
{
    state = &OuterDerivationGoal::getDerivation;
    name = fmt(
        "outer obtaining drv from '%s' and then building outputs %s",
        drvReq->to_string(worker.store),
        concatStringsSep(", ", quoteStrings(wantedOutputs)));
    trace("created outer");

    worker.updateProgress();
}


OuterDerivationGoal::~OuterDerivationGoal()
{
}


static StorePath pathPartOfReq(const SingleDerivedPath & req)
{
    return std::visit(overloaded {
        [&](const SingleDerivedPath::Opaque & bo) {
            return bo.path;
        },
        [&](const SingleDerivedPath::Built & bfd) {
            return pathPartOfReq(*bfd.drvPath);
        },
    }, req.raw());
}


string OuterDerivationGoal::key()
{
    /* Ensure that derivations get built in order of their name,
       i.e. a derivation named "aardvark" always comes before "baboon". And
       substitution goals and inner derivation goals always happen before
       derivation goals (due to "b$"). */
    return "c$" + std::string(pathPartOfReq(*drvReq).name()) + "$" + drvReq->to_string(worker.store);
}


void OuterDerivationGoal::timedOut(Error && ex)
{
}


void OuterDerivationGoal::work()
{
    (this->*state)();
}


void OuterDerivationGoal::addWantedOutputs(const StringSet & outputs)
{
    /* If we already want all outputs, there is nothing to do. */
    if (wantedOutputs.empty()) return;

    bool needRestart = true;
    if (outputs.empty()) {
        wantedOutputs.clear();
        needRestart = true;
    } else
        for (auto & i : outputs)
            if (wantedOutputs.insert(i).second)
                needRestart = true;
    if (!needRestart) return;

    if (!optDrvPath)
        // haven't started steps where the outputs matter yet
        return;
    worker.makeDerivationGoal(*optDrvPath, outputs, buildMode);
}


void OuterDerivationGoal::getDerivation()
{
    trace("outer init");

    /* The first thing to do is to make sure that the derivation
       exists.  If it doesn't, it may be created through a
       substitute. */
    {
        if (buildMode != bmNormal) goto load;
        auto drvReq2 = tryResolveDerivedPath(worker.store, *drvReq);
        auto drvPathP = std::get_if<DerivedPath::Opaque>(&drvReq2);
        if (!drvPathP) goto load;
        auto & drvPath = drvPathP->path;
        if (!worker.store.isValidPath(drvPath)) goto load;

        trace(fmt("already have drv '%s' for '%s', can go straight to building",
            worker.store.printStorePath(drvPath),
            drvReq->to_string(worker.store)));

        loadAndBuildDerivation();
        return;
    }

load:
    trace("need to obtain drv we want to build");

    addWaitee(worker.makeGoal(drvReq->to_multi()));

    state = &OuterDerivationGoal::loadAndBuildDerivation;
    if (waitees.empty()) work();
}


void OuterDerivationGoal::loadAndBuildDerivation()
{
    trace("outer load and build derivation");

    if (nrFailed != 0) {
        amDone(ecFailed, Error("cannot build missing derivation '%s'", drvReq->to_string(worker.store)));
        return;
    }

    StorePath drvPath = resolveDerivedPath(worker.store, *drvReq);
    /* Build this step! */
    addWaitee(upcast_goal(worker.makeDerivationGoal(drvPath, wantedOutputs, buildMode)));
    state = &OuterDerivationGoal::buildDone;
    optDrvPath = std::move(drvPath);
    if (waitees.empty()) work();
}


void OuterDerivationGoal::buildDone()
{
    trace("outer build done");

    if (nrFailed != 0)
        amDone(ecFailed, Error("building '%s' failed", drvReq->to_string(worker.store)));
    else
        amDone(ecSuccess);
}


}
