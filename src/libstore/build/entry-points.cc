#include "nix/store/derivations.hh"
#include "nix/store/build/worker.hh"
#include "nix/store/build/substitution-goal.hh"
#include "nix/store/build/derivation-trampoline-goal.hh"
#include "nix/store/local-store.hh"
#include "nix/util/strings.hh"

namespace nix {

void Store::buildPaths(const std::vector<DerivedPath> & reqs, BuildMode buildMode, std::shared_ptr<Store> evalStore)
{
    Worker worker(*this, evalStore ? *evalStore : *this);

    Goals goals;
    for (auto & br : reqs)
        goals.insert(worker.makeGoal(br, buildMode));

    worker.run(goals);

    StringSet failed;
    BuildResult::Failure * failure = nullptr;
    for (auto & i : goals) {
        if (auto * f = i->buildResult.tryGetFailure()) {
            if (failure)
                logError(f->info());
            else
                failure = f;
        }
        if (i->exitCode != Goal::ecSuccess) {
            if (auto i2 = dynamic_cast<DerivationTrampolineGoal *>(i.get()))
                failed.insert(i2->drvReq->to_string(*this));
            else if (auto i2 = dynamic_cast<PathSubstitutionGoal *>(i.get()))
                failed.insert(printStorePath(i2->storePath));
        }
    }

    if (failed.size() == 1 && failure) {
        failure->withExitStatus(worker.failingExitStatus());
        throw *failure;
    } else if (!failed.empty()) {
        if (failure)
            logError(failure->info());
        throw Error(worker.failingExitStatus(), "build of %s failed", concatStringsSep(", ", quoteStrings(failed)));
    }
}

std::vector<KeyedBuildResult> Store::buildPathsWithResults(
    const std::vector<DerivedPath> & reqs, BuildMode buildMode, std::shared_ptr<Store> evalStore)
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
    results.reserve(state.size());

    for (auto & [req, goalPtr] : state) {
        /* Goals that were never started or were cancelled have exitCode
           ecBusy and a default buildResult with empty errorMsg. Skip them
           to avoid reporting spurious failures with empty messages. */
        if (goalPtr->exitCode == Goal::ecBusy)
            continue;
        results.emplace_back(
            KeyedBuildResult{
                goalPtr->buildResult,
                /* .path = */ req,
            });
    }

    return results;
}

BuildResult Store::buildDerivation(const StorePath & drvPath, const BasicDerivation & drv, BuildMode buildMode)
{
    Worker worker(*this, *this);
    auto goal = worker.makeDerivationTrampolineGoal(drvPath, OutputsSpec::All{}, drv, buildMode);

    try {
        worker.run(Goals{goal});
        return goal->buildResult;
    } catch (Error & e) {
        return BuildResult{
            .inner = BuildResult::Failure{{
                .status = BuildResult::Failure::MiscFailure,
                .msg = e.msg(),
            }}};
    };
}

void Store::ensurePath(const StorePath & path)
{
    /* If the path is already valid, we're done. */
    if (isValidPath(path))
        return;

    Worker worker(*this, *this);
    GoalPtr goal = worker.makePathSubstitutionGoal(path);
    Goals goals = {goal};

    worker.run(goals);

    if (goal->exitCode != Goal::ecSuccess) {
        goal->buildResult.tryThrowBuildError(worker.failingExitStatus());
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
            goals.insert(worker.makeGoal(
                DerivedPath::Built{
                    .drvPath = makeConstantStorePathRef(*info->deriver),
                    // FIXME: Should just build the specific output we need.
                    .outputs = OutputsSpec::All{},
                },
                bmRepair));
            worker.run(goals);
        } else
            throw Error(worker.failingExitStatus(), "cannot repair path '%s'", printStorePath(path));
    }
}

} // namespace nix
