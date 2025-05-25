#include "nix/store/build/derivation-resolution-goal.hh"
#include "nix/util/finally.hh"
#include "nix/store/build/worker.hh"
#include "nix/store/build/substitution-goal.hh"
#include "nix/util/callback.hh"
#include "nix/store/derivations.hh"

namespace nix {

DerivationResolutionGoal::DerivationResolutionGoal(const StorePath & drvPath, Worker & worker)
    : Goal(worker, init())
    , drvPath(drvPath)
{
    name = fmt("resolution of '%s'", worker.store.printStorePath(drvPath));
    trace("created");
}

Goal::Co DerivationResolutionGoal::init()
{
    trace("init");

    std::unique_ptr<Derivation> drv;

    if (worker.evalStore.isValidPath(drvPath)) {
        drv = std::make_unique<Derivation>(worker.evalStore.readDerivation(drvPath));
    } else if (worker.store.isValidPath(drvPath)) {
        drv = std::make_unique<Derivation>(worker.store.readDerivation(drvPath));
    } else {
        auto goal0 = worker.makePathSubstitutionGoal(drvPath);
        goal0->preserveException = true;
        co_await await(Goals{goal0});
        if (nrFailed > 0)
            co_return amDone(goal0->exitCode, goal0->ex);

        drv = std::make_unique<Derivation>(worker.store.readDerivation(drvPath));
    }

    trace("output path substituted");

    std::set<std::shared_ptr<BuildTraceGoal>> goals;

    std::function<void(ref<SingleDerivedPath>, const DerivedPathMap<StringSet>::ChildNode &)> accumInputPaths;

    accumInputPaths = [&](ref<SingleDerivedPath> depDrvPath, const DerivedPathMap<StringSet>::ChildNode & inputNode) {
        for (auto & outputName : inputNode.value)
            goals.insert(worker.makeBuildTraceGoal(SingleDerivedPath::Built{depDrvPath, outputName}));

        for (auto & [outputName, childNode] : inputNode.childMap)
            accumInputPaths(make_ref<SingleDerivedPath>(SingleDerivedPath::Built{depDrvPath, outputName}), childNode);
    };

    for (auto & [depDrvPath, depNode] : drv->inputDrvs.map)
        accumInputPaths(makeConstantStorePathRef(depDrvPath), depNode);

    if (nrFailed > 0) {
        debug("TODO message");
        co_return amDone(nrNoSubstituters > 0 ? ecNoSubstituters : ecFailed);
    }

    if (true /*auto d = drv.tryResolve(....)*/) {
        //resolvedDerivation = d.take();

        trace("finished");
        co_return amDone(ecSuccess);
    } else {
        // fail
    }
}

std::string DerivationResolutionGoal::key()
{
    /* "a$" ensures substitution goals happen before derivation
       goals. */
    return "b$" + worker.store.printStorePath(drvPath);
}

void DerivationResolutionGoal::handleEOF(Descriptor fd)
{
    worker.wakeUp(shared_from_this());
}

}
