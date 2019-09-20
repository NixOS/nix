#include "build.hh"

namespace nix {

static void primeCache(Store & store, const PathSet & paths)
{
    PathSet willBuild, willSubstitute, unknown;
    unsigned long long downloadSize, narSize;
    store.queryMissing(paths, willBuild, willSubstitute, unknown, downloadSize, narSize);

    if (!willBuild.empty() && 0 == settings.maxBuildJobs && getMachines().empty())
        throw Error(
            "%d derivations need to be built, but neither local builds ('--max-jobs') "
            "nor remote builds ('--builders') are enabled", willBuild.size());
}


void LocalStore::buildPaths(const PathSet & drvPaths, BuildMode buildMode)
{
    Worker worker(*this);

    primeCache(*this, drvPaths);

    Goals goals;
    for (auto & i : drvPaths) {
        DrvPathWithOutputs i2 = parseDrvPathWithOutputs(i);
        if (isDerivation(i2.first))
            goals.insert(worker.makeDerivationGoal(i2.first, i2.second, buildMode));
        else
            goals.insert(worker.makeSubstitutionGoal(i, buildMode == bmRepair ? Repair : NoRepair));
    }

    worker.run(goals);

    PathSet failed;
    for (auto & i : goals) {
        if (i->getExitCode() != Goal::ecSuccess) {
            DerivationGoal * i2 = dynamic_cast<DerivationGoal *>(i.get());
            if (i2) failed.insert(i2->getDrvPath());
            else failed.insert(dynamic_cast<SubstitutionGoal *>(i.get())->getStorePath());
        }
    }

    if (!failed.empty())
        throw Error(worker.exitStatus(), "build of %s failed", showPaths(failed));
}


BuildResult LocalStore::buildDerivation(const Path & drvPath, const BasicDerivation & drv,
    BuildMode buildMode)
{
    Worker worker(*this);
    auto goal = worker.makeBasicDerivationGoal(drvPath, drv, buildMode);

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


void LocalStore::ensurePath(const Path & path)
{
    /* If the path is already valid, we're done. */
    if (isValidPath(path)) return;

    primeCache(*this, {path});

    Worker worker(*this);
    GoalPtr goal = worker.makeSubstitutionGoal(path);
    Goals goals = {goal};

    worker.run(goals);

    if (goal->getExitCode() != Goal::ecSuccess)
        throw Error(worker.exitStatus(), "path '%s' does not exist and cannot be created", path);
}


void LocalStore::repairPath(const Path & path)
{
    Worker worker(*this);
    GoalPtr goal = worker.makeSubstitutionGoal(path, Repair);
    Goals goals = {goal};

    worker.run(goals);

    if (goal->getExitCode() != Goal::ecSuccess) {
        /* Since substituting the path didn't work, if we have a valid
           deriver, then rebuild the deriver. */
        auto deriver = queryPathInfo(path)->deriver;
        if (deriver != "" && isValidPath(deriver)) {
            goals.clear();
            goals.insert(worker.makeDerivationGoal(deriver, StringSet(), bmRepair));
            worker.run(goals);
        } else
            throw Error(worker.exitStatus(), "cannot repair path '%s'", path);
    }
}

}
