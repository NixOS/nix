#include "nix/store/build/derivation-trampoline-goal.hh"
#include "nix/store/build/worker.hh"
#include "nix/store/derivations.hh"

#include <ranges>
#include <algorithm>

namespace nix {

DerivationTrampolineGoal::DerivationTrampolineGoal(
    ref<const SingleDerivedPath> drvReq, const OutputsSpec & wantedOutputs, Worker & worker, BuildMode buildMode)
    : Goal(worker, init())
    , drvReq(drvReq)
    , wantedOutputs(wantedOutputs)
    , buildMode(buildMode)
{
    commonInit();
}

DerivationTrampolineGoal::DerivationTrampolineGoal(
    const StorePath & drvPath,
    const OutputsSpec & wantedOutputs,
    const Derivation & drv,
    Worker & worker,
    BuildMode buildMode)
    : Goal(worker, haveDerivation(drvPath, drv))
    , drvReq(makeConstantStorePathRef(drvPath))
    , wantedOutputs(wantedOutputs)
    , buildMode(buildMode)
{
    commonInit();
}

void DerivationTrampolineGoal::commonInit()
{
    name =
        fmt("obtaining derivation from '%s' and then building outputs %s",
            drvReq->to_string(worker.store),
            std::visit(
                overloaded{
                    [&](const OutputsSpec::All) -> std::string { return "* (all of them)"; },
                    [&](const OutputsSpec::Names os) { return concatStringsSep(", ", quoteStrings(os)); },
                },
                wantedOutputs.raw));
    trace("created outer");

    worker.updateProgress();
}

DerivationTrampolineGoal::~DerivationTrampolineGoal() {}

std::string DerivationTrampolineGoal::key()
{
    auto pathPartOfReq = [](this const auto & self, const SingleDerivedPath & req) -> StorePath {
        return std::visit(
            overloaded{
                [&](const SingleDerivedPath::Opaque & bo) { return bo.path; },
                [&](const SingleDerivedPath::Built & bfd) { return self(*bfd.drvPath); },
            },
            req.raw());
    };

    return "da$" + std::string(pathPartOfReq(*drvReq).name()) + "$" + DerivedPath::Built{
        .drvPath = drvReq,
        .outputs = wantedOutputs,
    }.to_string(worker.store);
}

Goal::Co DerivationTrampolineGoal::init()
{
    trace("need to load derivation from file");

    /* The first thing to do is to make sure that the derivation
       exists.  If it doesn't, it may be built from another derivation,
       or merely substituted. We can make goal to get it and not worry
       about which method it takes to get the derivation. */
    if (auto optDrvPath = [this]() -> std::optional<StorePath> {
            if (buildMode != bmNormal)
                return std::nullopt;

            auto drvPath = StorePath::dummy;
            try {
                drvPath = resolveDerivedPath(worker.store, *drvReq);
            } catch (MissingRealisation &) {
                return std::nullopt;
            }
            auto cond = worker.evalStore.isValidPath(drvPath) || worker.store.isValidPath(drvPath);
            return cond ? std::optional{drvPath} : std::nullopt;
        }()) {
        trace(
            fmt("already have drv '%s' for '%s', can go straight to building",
                worker.store.printStorePath(*optDrvPath),
                drvReq->to_string(worker.store)));
    } else {
        trace("need to obtain drv we want to build");
        Goals waitees{worker.makeGoal(DerivedPath::fromSingle(*drvReq))};
        co_await await(std::move(waitees));
    }

    trace("outer load and build derivation");

    if (nrFailed != 0) {
        co_return doneFailure(
            ecFailed,
            BuildResult::Failure{{
                .status = BuildResult::Failure::DependencyFailed,
                .msg = HintFmt("failed to obtain derivation of '%s'", drvReq->to_string(worker.store)),
            }});
    }

    StorePath drvPath = resolveDerivedPath(worker.store, *drvReq);

    /* `drvPath' should already be a root, but let's be on the safe
       side: if the user forgot to make it a root, we wouldn't want
       things being garbage collected while we're busy. */
    worker.evalStore.addTempRoot(drvPath);

    /* Get the derivation. It is probably in the eval store, but it might be in the main store:

         - Resolved derivation are resolved against main store realisations, and so must be stored there.

         - Dynamic derivations are built, and so are found in the main store.
     */
    auto drv = [&] {
        for (auto * drvStore : {&worker.evalStore, &worker.store})
            if (drvStore->isValidPath(drvPath))
                return drvStore->readDerivation(drvPath);
        assert(false);
    }();

    co_return haveDerivation(std::move(drvPath), std::move(drv));
}

Goal::Co DerivationTrampolineGoal::haveDerivation(StorePath drvPath, Derivation drv)
{
    trace("have derivation, will kick off derivations goals per wanted output");

    auto resolvedWantedOutputs = std::visit(
        overloaded{
            [&](const OutputsSpec::Names & names) -> OutputsSpec::Names { return names; },
            [&](const OutputsSpec::All &) -> OutputsSpec::Names {
                StringSet outputs;
                for (auto & [outputName, _] : drv.outputs)
                    outputs.insert(outputName);
                return outputs;
            },
        },
        wantedOutputs.raw);

    /* Must have at least one wanted output. This is assumed below. */
    assert(!resolvedWantedOutputs.empty());

    Goals concreteDrvGoals;

    /* Build this step! */

    auto sharedDrv = make_ref<const Derivation>(std::move(drv));

    for (auto & output : resolvedWantedOutputs) {
        auto g = upcast_goal(worker.makeDerivationGoal(drvPath, sharedDrv, output, buildMode, false));
        g->preserveFailure = true;
        /* We will finish with it ourselves, as if we were the derivational goal. */
        concreteDrvGoals.insert(std::move(g));
    }

    co_await await(concreteDrvGoals);

    trace("outer build done");

    if (nrFailed != 0) {
        auto gi = std::ranges::find_if(concreteDrvGoals, [](const GoalPtr & goal) -> bool {
            auto exitCode = goal->exitCode;
            /* Note that without --keep-going waitees might be cancelled before
               we are woken up. */
            return exitCode != ecBusy && exitCode != ecSuccess;
        });

        const Goal * g = gi->get();
        assert(gi != concreteDrvGoals.end() && "expected a failing goal");
        auto exitCode = g->exitCode;
        const auto * failure = g->buildResult.tryGetFailure();
        assert(failure && "failing goal does not report a failed build result");

        /* Report the exit status of *some* failing goal. This might not be strictly
           correct, since multiple subgoals can fail independently, but this should be
           a good enough heuristic without --keep-going. */
        co_return doneFailure(exitCode, *failure);
    }

    SingleDrvOutputs outputs;

    auto successes = std::views::transform(concreteDrvGoals, [](const GoalPtr & a) -> const BuildResult::Success & {
        auto * success = a->buildResult.tryGetSuccess();
        assert(success && "goal succeeded, but some waitees do not report a successful status");
        return *success;
    });

    for (const auto & success : successes)
        std::ranges::copy(success.builtOutputs, std::inserter(outputs, outputs.end()));

    auto statuses = successes | std::views::transform(&BuildResult::Success::status);

    /* Aggregate the status code. If some outputs we already valid, but we had
       to build/substitute the other ones, report it as the smallest common
       denominator. */
    auto compareSuccesses = [](auto a, auto b) {
        /* This is technically an identity mapping of the underlying values, but
           it would be worse to rely on the enum ordering here. */
        auto toPriority = [](auto st) {
            using enum BuildResult::Success::Status;
            switch (st) {
            case Built:
                return 0;
            case Substituted:
                return 1;
            case AlreadyValid:
                return 2;
            case ResolvesToAlreadyValid:
                return 3;
            default:
                unreachable();
            }
        };
        return toPriority(a) < toPriority(b);
    };

    co_return doneSuccess({
        .status = std::ranges::min(statuses, compareSuccesses),
        .builtOutputs = std::move(outputs),
    });
}

} // namespace nix
