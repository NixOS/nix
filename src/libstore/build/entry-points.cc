#include "nix/store/derivations.hh"
#include "nix/store/build/worker.hh"
#include "nix/store/build/substitution-goal.hh"
#include "nix/store/build/derivation-trampoline-goal.hh"
#include "nix/util/strings.hh"

namespace nix {

void Worker::buildPaths(const std::vector<DerivedPath> & reqs, BuildMode buildMode)
{
    Goals goals;
    for (auto & br : reqs)
        goals.insert(makeGoal(br, buildMode));

    run(goals);

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
                failed.insert(i2->drvReq->to_string(store));
            else if (auto i2 = dynamic_cast<PathSubstitutionGoal *>(i.get()))
                failed.insert(store.printStorePath(i2->storePath));
        }
    }

    if (failed.size() == 1 && failure) {
        failure->withExitStatus(exitStatusFlags.failingExitStatus());
        throw *failure;
    } else if (!failed.empty()) {
        auto exitStatus = exitStatusFlags.failingExitStatus();
        if (failure)
            logError(failure->info());
        throw Error(exitStatus, "build of %s failed", concatStringsSep(", ", quoteStrings(failed)));
    }
}

std::vector<KeyedBuildResult> Worker::buildPathsWithResults(const std::vector<DerivedPath> & reqs, BuildMode buildMode)
{
    Goals goals;
    std::vector<std::pair<const DerivedPath &, GoalPtr>> state;

    for (const auto & req : reqs) {
        auto goal = makeGoal(req, buildMode);
        goals.insert(goal);
        state.push_back({req, goal});
    }

    run(goals);

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

BuildResult Worker::buildDerivation(const StorePath & drvPath, const BasicDerivation & drv, BuildMode buildMode)
{
    auto goal = makeDerivationTrampolineGoal(drvPath, OutputsSpec::All{}, drv, buildMode);

    try {
        run(Goals{goal});
        return goal->buildResult;
    } catch (Error & e) {
        return BuildResult{
            .inner = BuildResult::Failure{{
                .status = BuildResult::Failure::MiscFailure,
                .msg = e.msg(),
            }}};
    };
}

void Worker::ensurePath(const StorePath & path)
{
    /* If the path is already valid, we're done. */
    if (store.isValidPath(path))
        return;

    GoalPtr goal = makePathSubstitutionGoal(path);
    Goals goals = {goal};

    run(goals);

    if (goal->exitCode != Goal::ecSuccess) {
        auto exitStatus = exitStatusFlags.failingExitStatus();
        goal->buildResult.tryThrowBuildError(exitStatus);
        throw Error(exitStatus, "path '%s' does not exist and cannot be created", store.printStorePath(path));
    }
}

void Worker::repairPath(const StorePath & path)
{
    GoalPtr goal = makePathSubstitutionGoal(path, Repair);
    Goals goals = {goal};

    run(goals);

    if (goal->exitCode != Goal::ecSuccess) {
        /* Since substituting the path didn't work, if we have a valid
           deriver, then rebuild the deriver. */
        auto info = store.queryPathInfo(path);
        if (info->deriver && store.isValidPath(*info->deriver)) {
            goals.clear();
            goals.insert(makeGoal(
                DerivedPath::Built{
                    .drvPath = makeConstantStorePathRef(*info->deriver),
                    // FIXME: Should just build the specific output we need.
                    .outputs = OutputsSpec::All{},
                },
                bmRepair));
            run(goals);
        } else
            throw Error(exitStatusFlags.failingExitStatus(), "cannot repair path '%s'", store.printStorePath(path));
    }
}

} // namespace nix
