#include "worker.hh"
#include "substitution-goal.hh"
#include "derivation-goal.hh"
#include "local-store.hh"

namespace nix {

void Store::buildPaths(const std::vector<DerivedPath> & reqs, BuildMode buildMode, std::shared_ptr<Store> evalStore)
{
    Worker worker(*this, evalStore ? *evalStore : *this);

    Goals goals;
    for (auto & br : reqs)
        goals.insert(worker.makeGoal(br, buildMode));

    worker.run(goals);

    StorePathSet failed;
    std::optional<Error> ex;
    for (auto & i : goals) {
        if (i->ex) {
            if (ex)
                logError(i->ex->info());
            else
                ex = std::move(i->ex);
        }
        if (i->exitCode != Goal::ecSuccess) {
            if (auto i2 = dynamic_cast<DerivationGoal *>(i.get())) failed.insert(i2->drvPath);
            else if (auto i2 = dynamic_cast<PathSubstitutionGoal *>(i.get())) failed.insert(i2->storePath);
        }
    }

    if (failed.size() == 1 && ex) {
        ex->status = worker.failingExitStatus();
        throw std::move(*ex);
    } else if (!failed.empty()) {
        if (ex) logError(ex->info());
        throw Error(worker.failingExitStatus(), "build of %s failed", showPaths(failed));
    }
}

std::vector<KeyedBuildResult> Store::buildPathsWithResults(
    const std::vector<DerivedPath> & reqs,
    BuildMode buildMode,
    std::shared_ptr<Store> evalStore)
{
    Worker worker(*this, evalStore ? *evalStore : *this);

    Goals goals;
    std::vector<std::pair<const DerivedPath &, GoalPtr>> state;

    for (const auto & req : reqs) {
        auto goal = worker.makeGoal(req, buildMode);
        goals.insert(goal);
        state.push_back({req, goal});
    }

    worker.run(goals);

    std::vector<KeyedBuildResult> results;

    for (auto & [req, goalPtr] : state)
        results.emplace_back(KeyedBuildResult {
            goalPtr->getBuildResult(req),
            /* .path = */ req,
        });

    return results;
}

BuildResult Store::buildDerivation(const StorePath & drvPath, const BasicDerivation & drv,
    BuildMode buildMode)
{
    Worker worker(*this, *this);
    auto goal = worker.makeBasicDerivationGoal(drvPath, drv, OutputsSpec::All {}, buildMode);

    try {
        worker.run(Goals{goal});
        return goal->getBuildResult(DerivedPath::Built {
            .drvPath = drvPath,
            .outputs = OutputsSpec::All {},
        });
    } catch (Error & e) {
        return BuildResult {
            .status = BuildResult::MiscFailure,
            .errorMsg = e.msg(),
        };
    };
}


void Store::ensurePath(const StorePath & path)
{
    /* If the path is already valid, we're done. */
    if (isValidPath(path)) return;

    Worker worker(*this, *this);
    GoalPtr goal = worker.makePathSubstitutionGoal(path);
    Goals goals = {goal};

    worker.run(goals);

    if (goal->exitCode != Goal::ecSuccess) {
        if (goal->ex) {
            goal->ex->status = worker.failingExitStatus();
            throw std::move(*goal->ex);
        } else
            throw Error(worker.failingExitStatus(), "path '%s' does not exist and cannot be created", printStorePath(path));
    }
}


void Store::repairPath(const StorePath & path)
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
            // FIXME: Should just build the specific output we need.
            goals.insert(worker.makeDerivationGoal(*info->deriver, OutputsSpec::All { }, bmRepair));
            worker.run(goals);
        } else
            throw Error(worker.failingExitStatus(), "cannot repair path '%s'", printStorePath(path));
    }
}

}
