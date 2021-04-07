#include "machines.hh"
#include "worker.hh"
#include "substitution-goal.hh"
#include "outer-derivation-goal.hh"
#include "derivation-goal.hh"
#include "local-store.hh"

namespace nix {

void Store::buildPaths(const std::vector<DerivedPath> & reqs, BuildMode buildMode)
{
    Worker worker(*this);

    Goals goals;
    for (auto & br : reqs)
        goals.insert(worker.makeGoal(br, buildMode));

    worker.run(goals);

    StringSet failed;
    std::optional<Error> ex;
    for (auto & i : goals) {
        if (i->ex) {
            if (ex)
                logError(i->ex->info());
            else
                ex = i->ex;
        }
        if (i->exitCode != Goal::ecSuccess) {
            if (auto i2 = dynamic_cast<OuterDerivationGoal *>(i.get()))
                failed.insert(i2->drvReq->to_string(*this));
            else if (auto i2 = dynamic_cast<PathSubstitutionGoal *>(i.get()))
                failed.insert(printStorePath(i2->storePath));
        }
    }

    if (failed.size() == 1 && ex) {
        ex->status = worker.exitStatus();
        throw *ex;
    } else if (!failed.empty()) {
        if (ex) logError(ex->info());
        throw Error(worker.exitStatus(), "build of %s failed", concatStringsSep(", ", quoteStrings(failed)));
    }
}

BuildResult Store::buildDerivation(const StorePath & drvPath, const BasicDerivation & drv,
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
    // XXX: Should use `goal->queryPartialDerivationOutputMap()` once it's
    // extended to return the full realisation for each output
    auto staticDrvReqOutputs = drv.outputsAndOptPaths(*this);
    auto outputHashes = staticOutputHashes(*this, drv);
    for (auto & [outputName, staticOutput] : staticDrvReqOutputs) {
        auto outputId = DrvOutput{outputHashes.at(outputName), outputName};
        if (staticOutput.second)
            result.builtOutputs.insert_or_assign(
                    outputId,
                    Realisation{ outputId, *staticOutput.second}
                    );
        if (settings.isExperimentalFeatureEnabled("ca-derivations") && !derivationHasKnownOutputPaths(drv.type())) {
            auto realisation = this->queryRealisation(outputId);
            if (realisation)
                result.builtOutputs.insert_or_assign(
                        outputId,
                        *realisation
                        );
        }
    }

    return result;
}


void Store::ensurePath(const StorePath & path)
{
    /* If the path is already valid, we're done. */
    if (isValidPath(path)) return;

    Worker worker(*this);
    GoalPtr goal = worker.makePathSubstitutionGoal(path);
    Goals goals = {goal};

    worker.run(goals);

    if (goal->exitCode != Goal::ecSuccess) {
        if (goal->ex) {
            goal->ex->status = worker.exitStatus();
            throw *goal->ex;
        } else
            throw Error(worker.exitStatus(), "path '%s' does not exist and cannot be created", printStorePath(path));
    }
}


void LocalStore::repairPath(const StorePath & path)
{
    Worker worker(*this);
    GoalPtr goal = worker.makePathSubstitutionGoal(path, Repair);
    Goals goals = {goal};

    worker.run(goals);

    if (goal->exitCode != Goal::ecSuccess) {
        /* Since substituting the path didn't work, if we have a valid
           deriver, then rebuild the deriver. */
        auto info = queryPathInfo(path);
        if (info->deriver && isValidPath(*info->deriver)) {
            goals.clear();
            goals.insert(worker.makeGoal(DerivedPath::Built { staticDrvReq(*info->deriver) }, bmRepair));
            worker.run(goals);
        } else
            throw Error(worker.exitStatus(), "cannot repair path '%s'", printStorePath(path));
    }
}

}
