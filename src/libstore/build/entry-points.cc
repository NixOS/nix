#include "worker.hh"
#include "substitution-goal.hh"
#include "derivation-goal.hh"
#include "local-store.hh"

namespace nix {

void Store::buildPaths(const std::vector<DerivedPath> & reqs, BuildMode buildMode, std::shared_ptr<Store> evalStore)
{
    Worker worker(*this, evalStore ? *evalStore : *this);

    Goals goals;
    for (const auto & br : reqs) {
        std::visit(overloaded {
            [&](const DerivedPath::Built & bfd) {
                goals.insert(worker.makeDerivationGoal(bfd.drvPath, bfd.outputs, buildMode));
            },
            [&](const DerivedPath::Opaque & bo) {
                goals.insert(worker.makePathSubstitutionGoal(bo.path, buildMode == bmRepair ? Repair : NoRepair));
            },
        }, br.raw());
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
            if (auto i2 = dynamic_cast<DerivationGoal *>(i.get())) failed.insert(i2->drvPath);
            else if (auto i2 = dynamic_cast<PathSubstitutionGoal *>(i.get())) failed.insert(i2->storePath);
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

std::vector<BuildResult> Store::buildPathsWithResults(
    const std::vector<DerivedPath> & reqs,
    BuildMode buildMode,
    std::shared_ptr<Store> evalStore)
{
    Worker worker(*this, evalStore ? *evalStore : *this);

    Goals goals;
    for (const auto & br : reqs) {
        std::visit(overloaded {
            [&](const DerivedPath::Built & bfd) {
                goals.insert(worker.makeDerivationGoal(bfd.drvPath, bfd.outputs, buildMode));
            },
            [&](const DerivedPath::Opaque & bo) {
                goals.insert(worker.makePathSubstitutionGoal(bo.path, buildMode == bmRepair ? Repair : NoRepair));
            },
        }, br.raw());
    }

    worker.run(goals);

    std::vector<BuildResult> results;

    for (auto & i : goals)
        results.push_back(i->buildResult);

    return results;
}

BuildResult Store::buildDerivation(const StorePath & drvPath, const BasicDerivation & drv,
    BuildMode buildMode)
{
    Worker worker(*this, *this);
    auto goal = worker.makeBasicDerivationGoal(drvPath, drv, {}, buildMode);

    try {
        worker.run(Goals{goal});
        return goal->buildResult;
    } catch (Error & e) {
        return BuildResult {
            .status = BuildResult::MiscFailure,
            .errorMsg = e.msg(),
            .path = DerivedPath::Built { .drvPath = drvPath },
        };
    };
}


void Store::ensurePath(StorePathOrDesc path)
{
    /* If the path is already valid, we're done. */
    if (isValidPath(path)) return;

    Worker worker(*this, *this);
    GoalPtr goal = worker.makePathSubstitutionGoal(path);
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
    Worker worker(*this, *this);
    GoalPtr goal = worker.makePathSubstitutionGoal(path, Repair);
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
