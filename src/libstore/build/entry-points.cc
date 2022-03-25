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

std::vector<KeyedBuildResult> Store::buildPathsWithResults(
    const std::vector<DerivedPath> & reqs,
    BuildMode buildMode,
    std::shared_ptr<Store> evalStore)
{
    Worker worker(*this, evalStore ? *evalStore : *this);

    Goals goals;
    std::vector<std::pair<GoalPtr, const DerivedPath &>> reqs2;

    for (const auto & req : reqs) {
        auto g = std::visit(overloaded {
            [&](const DerivedPath::Built & bfd) -> GoalPtr {
                return worker.makeDerivationGoal(bfd.drvPath, bfd.outputs, buildMode);
            },
            [&](const DerivedPath::Opaque & bo) -> GoalPtr {
                return worker.makePathSubstitutionGoal(bo.path, buildMode == bmRepair ? Repair : NoRepair);
            },
        }, req.raw());
        goals.insert(g);
        reqs2.push_back({g, req});
    }

    worker.run(goals);

    std::vector<KeyedBuildResult> results;

    for (auto & [gp, req] : reqs2) {
        auto & br = results.emplace_back(KeyedBuildResult {
            gp->buildResult,
            .path = req
        });

        auto pbp = std::get_if<DerivedPath::Built>(&req);
        if (!pbp) continue;
        auto & bp = *pbp;

        /* Because goals are in general shared between derived paths
           that share the same derivation, we need to filter their
           results to get back just the results we care about.
         */

        auto & bos = br.builtOutputs;

        for (auto it = bos.begin(); it != bos.end();) {
            if (wantOutput(it->first.outputName, bp.outputs))
                ++it;
            else
                it = bos.erase(it);
        }
    }

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
            goal->ex->status = worker.exitStatus();
            throw *goal->ex;
        } else
            throw Error(worker.exitStatus(), "path '%s' does not exist and cannot be created", printStorePath(path));
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
