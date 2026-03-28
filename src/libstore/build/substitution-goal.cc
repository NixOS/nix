#include "goal-impl.hh"

#include "nix/store/build/worker.hh"
#include "nix/store/build/substitution-goal.hh"
#include "nix/store/nar-info.hh"
#include "nix/util/finally.hh"
#include "nix/util/signals.hh"
#include "nix/util/callback.hh"
#include "nix/store/globals.hh"

#include <coroutine>

namespace nix {

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

PathSubstitutionGoal::~PathSubstitutionGoal()
{
    cleanup();
}

Goal::Co PathSubstitutionGoal::init()
{
    trace("init");

    worker.store.addTempRoot(storePath);

    /* If the path already exists we're done. */
    if (!repair && worker.store.isValidPath(storePath)) {
        co_return doneSuccess(BuildResult::Success{.status = BuildResult::Success::AlreadyValid});
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

        cleanup();

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
            info = co_await AsyncCallback<ref<const ValidPathInfo>>(
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

        // FIXME: consider returning boolean instead of passing in reference
        bool out = false; // is mutated by tryToRun
        co_await tryToRun(subPath ? *subPath : storePath, sub, info, out);
        substituterFailed = substituterFailed || out;
    }

    /* None left.  Terminate this goal and let someone else deal
       with it. */

    if (substituterFailed) {
        worker.failedSubstitutions++;
        worker.updateProgress();
    }
    if (lastStoresException.has_value()) {
        if (!worker.settings.tryFallback) {
            throw *lastStoresException;
        } else
            logError(lastStoresException->info());
    }

    /* Hack: don't indicate failure if there were no substituters.
       In that case the calling derivation should just do a
       build. */
    co_return doneFailure(
        substituterFailed ? ecFailed : ecNoSubstituters,
        BuildResult::Failure{{
            .status = BuildResult::Failure::NoSubstituters,
            .msg = HintFmt(
                "path '%s' is required, but there is no substituter that can build it",
                worker.store.printStorePath(storePath)),
        }});
}

Goal::Co PathSubstitutionGoal::tryToRun(
    StorePath subPath, nix::ref<Store> sub, std::shared_ptr<const ValidPathInfo> info, bool & substituterFailed)
{
    trace("all references realised");

    if (nrFailed > 0) {
        co_return doneFailure(
            nrNoSubstituters > 0 ? ecNoSubstituters : ecFailed,
            BuildResult::Failure{{
                .status = BuildResult::Failure::DependencyFailed,
                .msg = HintFmt(
                    "some references of path '%s' could not be realised", worker.store.printStorePath(storePath)),
            }});
    }

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

    co_await yield();

    trace("trying to run");

    /* Make sure that we are allowed to start a substitution.  Note that even
       if maxSubstitutionJobs == 0, we still allow a substituter to run. This
       prevents infinite waiting. */
    while (worker.getNrSubstitutions() >= std::max(1U, (unsigned int) worker.settings.maxSubstitutionJobs)) {
        co_await waitForBuildSlot();
    }

    auto maintainRunningSubstitutions = std::make_unique<MaintainCount<uint64_t>>(worker.runningSubstitutions);
    worker.updateProgress();

    auto promise = std::promise<void>();
    auto future = promise.get_future();

    /* Be careful with ownership. cleanup() doesn't signal the worker thread
       to cleanly shutdown, so the worker can die while the thread is still
       running. That's why we use weak_ptr for everything that is owned by the
       Worker. */
    thr = std::thread([weakGoal = weak_from_this(),
                       promise = std::move(promise),
                       subPath,
                       storePath = storePath,
                       repair = repair,
                       sub,
                       maybeWaker = worker.getCrossThreadWaker(),
                       maybeWorkerStore = worker.store.weak_from_this()]() mutable {
        try {
            ReceiveInterrupts receiveInterrupts;

            /* The Worker might have died while we were starting up. */
            auto workerStore = maybeWorkerStore.lock();
            if (!workerStore)
                return;

            Activity act(
                *logger,
                actSubstitute,
                Logger::Fields{workerStore->printStorePath(storePath), sub->config.getHumanReadableURI()});
            PushActivity pact(act.id);

            copyStorePath(*sub, *workerStore, subPath, repair, sub->config.isTrusted ? NoCheckSigs : CheckSigs);

            promise.set_value();
        } catch (...) {
            promise.set_exception(std::current_exception());
        }

        /* The Worker might have already died (and the waker with it) by the
           time we finished. N.B. if enqueueing to the waker throws, we better
           std::terminate, since something has gone very wrong. This intentionally
           lets the thread crash on exceptions for that reason. */
        if (auto waker = maybeWaker.lock())
            waker->enqueue(weakGoal);
    });

    /* Use up the substitution slot. */
    worker.childStarted(shared_from_this(), /*channels=*/{}, /*inBuildSlot=*/true, /*respectTimeouts=*/false);
    /* Suspend until the thread finishes. */
    co_await waitUntilWoken();

    trace("substitute finished");

    thr.join();
    worker.childTerminated(this);

    try {
        future.get();
    } catch (std::exception & e) {
        /* Cause the parent build to fail unless --fallback is given,
           or the substitute has disappeared. The latter case behaves
           the same as the substitute never having existed in the
           first place. */
        try {
            throw;
        } catch (SubstituteGone & sg) {
            /* Missing NARs are expected when they've been garbage collected.
               This is not a failure, so log as a warning instead of an error. */
            logWarning({.msg = sg.info().msg});
        } catch (...) {
            printError(e.what());
            substituterFailed = true;
        }

        co_return Return{};
    }

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

    co_return doneSuccess(BuildResult::Success{.status = BuildResult::Success::Substituted});
}

void PathSubstitutionGoal::cleanup()
{
    try {
        if (thr.joinable()) {
            // FIXME: signal worker thread to quit.
            thr.join();
            worker.childTerminated(this, JobCategory::Substitution);
        }
    } catch (...) {
        ignoreExceptionInDestructor();
    }
}

} // namespace nix
