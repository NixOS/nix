#include "machines.hh"
#include "worker.hh"
#include "substitution-goal.hh"
#include "derivation-goal.hh"

namespace nix {

static void primeCache(Store & store, const std::vector<StorePathWithOutputs> & paths)
{
    StorePathSet willBuild, willSubstitute, unknown;
    uint64_t downloadSize, narSize;
    store.queryMissing(paths, willBuild, willSubstitute, unknown, downloadSize, narSize);

    if (!willBuild.empty() && 0 == settings.maxBuildJobs && getMachines().empty())
        throw Error(
            "%d derivations need to be built, but neither local builds ('--max-jobs') "
            "nor remote builds ('--builders') are enabled", willBuild.size());
}


void LocalStore::buildPaths(const std::vector<StorePathWithOutputs> & drvPaths, BuildMode buildMode)
{
    Worker worker(*this);

    primeCache(*this, drvPaths);

    Goals goals;
    for (auto & path : drvPaths) {
        if (path.path.isDerivation())
            goals.insert(worker.makeDerivationGoal(path.path, path.outputs, buildMode));
        else
            goals.insert(worker.makeSubstitutionGoal(path.path, buildMode == bmRepair ? Repair : NoRepair));
    }

    worker.run(goals);

    StorePathSet failed;
    std::optional<Error> ex;
    for (auto & i : goals) {
        if (i->ex) {
            if (ex)
                logError(i->ex->info());
            else
                ex = i->ex;
        }
        if (i->exitCode != Goal::ecSuccess) {
            DerivationGoal * i2 = dynamic_cast<DerivationGoal *>(i.get());
            if (i2) failed.insert(i2->getDrvPath());
            else failed.insert(dynamic_cast<SubstitutionGoal *>(i.get())->getStorePath());
        }
    }

    if (failed.size() == 1 && ex) {
        ex->status = worker.exitStatus();
        throw *ex;
    } else if (!failed.empty()) {
        if (ex) logError(ex->info());
        throw Error(worker.exitStatus(), "build of %s failed", showPaths(failed));
    }
}

BuildResult LocalStore::buildDerivation(const StorePath & drvPath, const BasicDerivation & drv,
    BuildMode buildMode)
{
    Worker worker(*this);
    auto goal = worker.makeBasicDerivationGoal(drvPath, drv, {}, buildMode);

    BuildResult result;

    try {
        worker.run(Goals{goal});
        result = goal->getResult();
    } catch (Error & e) {
        result.status = BuildResult::MiscFailure;
        result.errorMsg = e.msg();
    }

    return result;
}


void LocalStore::ensurePath(StorePathOrDesc path)
{
    /* If the path is already valid, we're done. */
    if (isValidPath(path)) return;

    // primeCache(*this, {{path}});

    Worker worker(*this);
    GoalPtr goal = worker.makeSubstitutionGoal(path, NoRepair);
    Goals goals = {goal};

    worker.run(goals);

    if (goal->exitCode != Goal::ecSuccess) {
        if (goal->ex) {
            goal->ex->status = worker.exitStatus();
            throw *goal->ex;
        } else {
            auto p = this->bakeCaIfNeeded(path);
            throw Error(worker.exitStatus(), "path '%s' does not exist and cannot be created", printStorePath(p));
        }
    }
}


void LocalStore::repairPath(const StorePath & path)
{
    Worker worker(*this);
    GoalPtr goal = worker.makeSubstitutionGoal(path, Repair);
    Goals goals = {goal};

    worker.run(goals);

    if (goal->exitCode != Goal::ecSuccess) {
        /* Since substituting the path didn't work, if we have a valid
           deriver, then rebuild the deriver. */
        auto info = queryPathInfo(path);
        if (info->deriver && isValidPath(*info->deriver)) {
            goals.clear();
            goals.insert(worker.makeDerivationGoal(*info->deriver, StringSet(), bmRepair));
            worker.run(goals);
        } else
            throw Error(worker.exitStatus(), "cannot repair path '%s'", printStorePath(path));
    }
}

}
