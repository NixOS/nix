#include "nix/store/derivations.hh"
#include "nix/store/build/worker.hh"
#include "nix/store/build/substitution-goal.hh"
#include "nix/store/build/derivation-trampoline-goal.hh"
#include "nix/store/local-store.hh"

namespace nix {

void Store::buildPaths(const std::vector<DerivedPath> & reqs, BuildMode buildMode, std::shared_ptr<Store> evalStore)
{
    auto results = buildPathsWithResults(reqs, buildMode, evalStore);
    throwBuildResultErrors(results, *this);
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
        auto exitStatus = worker.exitStatusFlags.failingExitStatus();
        goal->buildResult.tryThrowBuildError(exitStatus);
        throw Error(exitStatus, "path '%s' does not exist and cannot be created", printStorePath(path));
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
            throw Error(worker.exitStatusFlags.failingExitStatus(), "cannot repair path '%s'", printStorePath(path));
    }
}

} // namespace nix
