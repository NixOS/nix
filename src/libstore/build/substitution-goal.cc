
#include "nix/store/build/worker.hh"
#include "nix/store/build/substitution-goal.hh"
#include "nix/store/build/substitution-plan-goal.hh"
#include "nix/store/nar-info.hh"
#include "nix/store/worker-settings.hh"
#include "nix/util/signals.hh"
#include "nix/util/callback.hh"

#include <array>

namespace nix {

/* FIXME: Deduplicate this please. */
static bool isCancellationException(std::exception_ptr ex)
{
    try {
        std::rethrow_exception(ex);
    } catch (const Interrupted &) {
        return true;
    } catch (const Cancelled &) {
        return true;
    } catch (const boost::system::system_error & e) {
        return e.code() == asio::error::operation_aborted;
    } catch (...) {
        return false;
    }
}

PathSubstitutionGoal::PathSubstitutionGoal(
    const StorePath & storePath, Worker & worker, RepairFlag repair, std::optional<ContentAddress> ca)
    : Goal(worker, init())
    , storePath(storePath)
    , repair(repair)
    , ca(ca)
{
    name = fmt("substitution of '%s'", worker.store.printStorePath(this->storePath));
    trace("created");
    maintainExpectedSubstitutions = std::make_unique<MaintainCount<uint64_t>>(worker.expectedSubstitutions);
}

PathSubstitutionGoal::~PathSubstitutionGoal() {}

asio::awaitable<Goal::ExitCode> PathSubstitutionGoal::init()
{
    auto result = co_await substitute();
    buildResult = std::move(result.second);
    co_return result.first;
}

asio::awaitable<PathSubstitutionGoal::Result> PathSubstitutionGoal::substitute()
{
    trace("init");

    worker.store.addTempRoot(storePath);

    /* If the path already exists we're done. */
    if (!repair && worker.store.isValidPath(storePath)) {
        co_return Result{
            ecSuccess,
            BuildResult{.inner = BuildResult::Success{.status = BuildResult::Success::AlreadyValid}},
        };
    }

    if (worker.store.config.getReadOnly())
        throw Error(
            "cannot substitute path '%s' - no write access to the Nix store", worker.store.printStorePath(storePath));

    /* Find out where to substitute the path from. */
    std::optional<SubstitutionPlanGoal::Candidate> candidate;

    if (repair && worker.store.isValidPath(storePath)) {
        /* Refetching a path that is valid (but corrupt) is not something
           a plan would ever include, so look directly. */
        auto subs = worker.getSubstituters();
        candidate = co_await SubstitutionPlanGoal::findSubstitute(worker, subs, storePath, ca);
    } else {
        /* This is the planning that a dry run does; a dry run right
           before us has done it already. */
        auto plan = worker.makeSubstitutionPlanGoal(storePath, ca);
        co_await await({plan});
        nrFailed = nrNoSubstituters = 0;
        candidate = plan->candidate;
    }

    /* Hack: don't indicate failure if there were no substituters.
       In that case the calling derivation should just do a
       build. */
    if (!candidate)
        co_return Result{
            ecNoSubstituters,
            BuildResult{
                .inner = BuildResult::Failure{{
                    .status = BuildResult::Failure::NoSubstituters,
                    .msg = HintFmt(
                        "path '%s' is required, but there is no substituter that can build it",
                        worker.store.printStorePath(storePath)),
                }}},
        };

    auto sub = candidate->sub;
    auto subPath = candidate->subPath;
    auto info = candidate->info;

    /* Update the total expected download size. */
    auto narInfo = std::dynamic_pointer_cast<const NarInfo>(info);

    maintainExpectedNar = std::make_unique<MaintainCount<uint64_t>>(worker.expectedNarSize, info->narSize);

    maintainExpectedDownload =
        narInfo && narInfo->fileSize
            ? std::make_unique<MaintainCount<uint64_t>>(worker.expectedDownloadSize, narInfo->fileSize)
            : nullptr;

    worker.updateProgress();

    Goals waitees;

    /* To maintain the closure invariant, we first have to realise the
       paths referenced by this one. */
    for (auto & i : info->references)
        if (i != storePath) /* ignore self-references */
            waitees.insert(worker.makePathSubstitutionGoal(i));

    co_await await(std::move(waitees));

    if (nrFailed > 0) {
        co_return Result{
            nrNoSubstituters > 0 ? ecNoSubstituters : ecFailed,
            BuildResult{
                .inner = BuildResult::Failure{{
                    .status = BuildResult::Failure::DependencyFailed,
                    .msg = HintFmt(
                        "some references of path '%s' could not be realised", worker.store.printStorePath(storePath)),
                }}},
        };
    }

    switch (co_await tryToRun(subPath, sub, info)) {
    case SubstitutionResult::Ok:
        co_return Result{
            ecSuccess,
            BuildResult{.inner = BuildResult::Success{.status = BuildResult::Success::Substituted}},
        };

    case SubstitutionResult::SubstituteGone:
        /* The substitute has disappeared since it was planned. That is
           the same as it never having existed: no substituter has the
           path, so a derivation should just be built. */
        co_return Result{
            ecNoSubstituters,
            BuildResult{
                .inner = BuildResult::Failure{{
                    .status = BuildResult::Failure::NoSubstituters,
                    .msg = HintFmt(
                        "path '%s' is required, but there is no substituter that can build it",
                        worker.store.printStorePath(storePath)),
                }}},
        };

    case SubstitutionResult::SubstituterFailed:
        /* The substituter failed to provide what it said it had. That
           is a failure of the substituter; the plan is not remade
           around it. */
        worker.failedSubstitutions++;
        worker.updateProgress();

        co_return Result{
            ecFailed,
            BuildResult{
                .inner = BuildResult::Failure{{
                    .status = BuildResult::Failure::TransientFailure,
                    .msg = HintFmt(
                        "substituter '%s' failed to provide path '%s'",
                        sub->config.getHumanReadableURI(),
                        worker.store.printStorePath(storePath)),
                }}},
        };
    }

    unreachable();
}

asio::awaitable<PathSubstitutionGoal::SubstitutionResult>
PathSubstitutionGoal::tryToRun(StorePath subPath, nix::ref<Store> sub, std::shared_ptr<const ValidPathInfo> info)
{
    trace("all references realised");

    for (auto & i : info->references)
        /* ignore self-references */
        if (i != storePath) {
            if (!worker.store.isValidPath(i)) {
                throw Error(
                    "reference '%s' of path '%s' is not a valid path",
                    worker.store.printStorePath(i),
                    worker.store.printStorePath(storePath));
            }
        }

    trace("trying to run");

    /* Make sure that we are allowed to start a substitution.  Note that even
       if maxSubstitutionJobs == 0, we still allow a substituter to run. This
       prevents infinite waiting. */
    auto slot = co_await worker.substitutionSemaphore.asyncAcquire();

    auto maintainRunningSubstitutions = std::make_unique<MaintainCount<uint64_t>>(worker.runningSubstitutions);
    worker.updateProgress();

    try {
        /* Cause the parent build to fail unless --fallback is given,
           or the substitute has disappeared. The latter case behaves
           the same as the substitute never having existed in the
           first place. */
        co_await asio::co_spawn(
            worker.getThreadPool(),
            [this, subPath, sub]() -> asio::awaitable<void> {
                /* TODO: Handle cancellations please. */
                ReceiveInterrupts receiveInterrupts;
                Activity act(
                    *logger,
                    actSubstitute,
                    std::to_array<Logger::Field>(
                        {worker.store.printStorePath(storePath), sub->config.getHumanReadableURI()}));
                PushActivity pact(act.id);
                copyStorePath(*sub, worker.store, subPath, repair, sub->config.isTrusted ? NoCheckSigs : CheckSigs);
                co_return;
            },
            asio::use_awaitable);
    } catch (SubstituteGone & sg) {
        /* Missing NARs are expected when they've been garbage collected.
           This is not a failure, so log as a warning instead of an error. */
        logWarning({.msg = sg.info().msg});
        co_return SubstitutionResult::SubstituteGone;
    } catch (std::exception & e) {
        if (isCancellationException(std::current_exception()))
            throw;
        printError(e.what());
        co_return SubstitutionResult::SubstituterFailed;
    }

    trace("substitute finished");

    worker.markContentsGood(storePath);

    printMsg(lvlChatty, "substitution of path '%s' succeeded", worker.store.printStorePath(storePath));

    maintainRunningSubstitutions.reset();

    maintainExpectedSubstitutions.reset();
    worker.doneSubstitutions++;

    if (maintainExpectedDownload) {
        auto fileSize = maintainExpectedDownload->delta;
        maintainExpectedDownload.reset();
        worker.doneDownloadSize += fileSize;
    }

    assert(maintainExpectedNar);
    worker.doneNarSize += maintainExpectedNar->delta;
    maintainExpectedNar.reset();

    worker.updateProgress();

    co_return SubstitutionResult::Ok;
}

} // namespace nix
