
#include "nix/store/build/worker.hh"
#include "nix/store/build/substitution-goal.hh"
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

    auto subs = worker.getSubstituters();

    bool substituterFailed = false;
    std::optional<Error> lastStoresException = std::nullopt;

    for (const auto & sub : subs) {
        trace("trying next substituter");
        if (lastStoresException.has_value()) {
            logError(lastStoresException->info());
            lastStoresException.reset();
        }

        /* The path the substituter refers to the path as. This will be
         * different when the stores have different names. */
        std::optional<StorePath> subPath;

        /* Path info returned by the substituter's query info operation. */
        std::shared_ptr<const ValidPathInfo> info;

        if (ca) {
            subPath = sub->makeFixedOutputPathFromCA(
                std::string{storePath.name()}, ContentAddressWithReferences::withoutRefs(*ca));
            if (sub->storeDir == worker.store.storeDir)
                assert(subPath == storePath);
        } else if (sub->storeDir != worker.store.storeDir) {
            continue;
        }

        try {
            info = co_await callbackToAwaitable<ref<const ValidPathInfo>>(
                [sub, path = subPath.value_or(storePath)](auto cb) { sub->queryPathInfo(path, std::move(cb)); });
        } catch (InvalidPath &) {
            continue;
        } catch (SubstituterDisabled & e) {
            continue;
        } catch (Error & e) {
            lastStoresException = std::make_optional(std::move(e));
            continue;
        }

        if (info->path != storePath) {
            if (info->isContentAddressed(*sub) && info->references.empty()) {
                auto info2 = std::make_shared<ValidPathInfo>(*info);
                info2->path = storePath;
                info = info2;
            } else {
                printError(
                    "asked '%s' for '%s' but got '%s'",
                    sub->config.getHumanReadableURI(),
                    worker.store.printStorePath(storePath),
                    sub->printStorePath(info->path));
                continue;
            }
        }

        /* Update the total expected download size. */
        auto narInfo = std::dynamic_pointer_cast<const NarInfo>(info);

        maintainExpectedNar = std::make_unique<MaintainCount<uint64_t>>(worker.expectedNarSize, info->narSize);

        maintainExpectedDownload =
            narInfo && narInfo->fileSize
                ? std::make_unique<MaintainCount<uint64_t>>(worker.expectedDownloadSize, narInfo->fileSize)
                : nullptr;

        worker.updateProgress();

        /* Bail out early if this substituter lacks a valid
           signature. LocalStore::addToStore() also checks for this, but
           only after we've downloaded the path. */
        if (!sub->config.isTrusted && worker.store.pathInfoIsUntrusted(*info)) {
            warn(
                "ignoring substitute for '%s' from '%s', as it's not signed by any of the keys in 'trusted-public-keys'",
                worker.store.printStorePath(storePath),
                sub->config.getHumanReadableURI());
            continue;
        }

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
                            "some references of path '%s' could not be realised",
                            worker.store.printStorePath(storePath)),
                    }}},
            };
        }

        SubstitutionResult res = co_await tryToRun(subPath ? *subPath : storePath, sub, info);
        if (res == SubstitutionResult::Ok)
            co_return Result{
                ecSuccess,
                BuildResult{.inner = BuildResult::Success{.status = BuildResult::Success::Substituted}},
            };

        substituterFailed = substituterFailed || (res == SubstitutionResult::SubstituterFailed);
    }

    /* None left.  Terminate this goal and let someone else deal
       with it. */

    if (substituterFailed) {
        worker.failedSubstitutions++;
        worker.updateProgress();
    }
    if (lastStoresException.has_value()) {
        if (!worker.settings.tryFallback) {
            throw std::move(*lastStoresException);
        } else
            logError(lastStoresException->info());
    }

    /* Hack: don't indicate failure if there were no substituters.
       In that case the calling derivation should just do a
       build. */
    co_return Result{
        substituterFailed ? ecFailed : ecNoSubstituters,
        BuildResult{
            .inner = BuildResult::Failure{{
                .status = BuildResult::Failure::NoSubstituters,
                .msg = HintFmt(
                    "path '%s' is required, but there is no substituter that can build it",
                    worker.store.printStorePath(storePath)),
            }}},
    };
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
