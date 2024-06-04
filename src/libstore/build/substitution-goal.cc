#include "worker.hh"
#include "substitution-goal.hh"
#include "nar-info.hh"
#include "finally.hh"
#include "signals.hh"
#include <coroutine>

namespace nix {

using Co = nix::PathSubstitutionGoal::Co;
using promise_type = nix::PathSubstitutionGoal::promise_type;
using handle_type = nix::PathSubstitutionGoal::handle_type;
using SuspendGoalAwaiter = nix::PathSubstitutionGoal::SuspendGoalAwaiter;
using SuspendGoal = nix::PathSubstitutionGoal::SuspendGoal;

Co::Co(Co&& rhs) {
    this->handle = rhs.handle;
    rhs.handle = nullptr;
}
void Co::operator=(Co&& rhs) {
    this->handle = rhs.handle;
    rhs.handle = nullptr;
}
Co::~Co() {
    std::clog << "destroying coroutine" << std::endl;
    if (handle) {
        assert(handle);
        std::clog << "destroying coroutine for " << handle.promise().loc.function_name() << std::endl;
        handle.promise().alive = false;
        // assert(handle.done());
        handle.destroy();
    } else {
        std::clog << "empty coroutine destroyed" << std::endl;
    }
}

Co promise_type::get_return_object() {
    auto handle = handle_type::from_promise(*this);
    return Co{handle};
};
// Here we execute our continuation, by passing it back to the caller.
// C++ compiler will create code that takes that and executes it promptly.
// `h` is the handle for the coroutine that is finishing execution,
// thus it must be destroyed.
std::coroutine_handle<> promise_type::final_awaiter::await_suspend(handle_type h) noexcept {
    auto& p = h.promise();
    p.goal.trace("in final_awaiter");
    // we are still on-going
    if (p.goal.exitCode == ecBusy) {
        p.goal.trace("we're busy");
        assert(p.alive); // sanity check to make sure it's not been destructed prematurely
        assert(p.goal.top_co);
        assert(p.goal.top_co->handle == h);
        // we move continuation to the top,
        // note: previous top_co is actually h, so by moving into it,
        // we're calling the destructor on h, DON'T use h and p after this!
        auto c = std::move(p.continuation);
        assert(c);
        auto& goal = p.goal;
        goal.top_co = std::move(c);
        goal.trace(fmt("jumping to %s", goal.top_co->handle.promise().loc.function_name()));
        return goal.top_co->handle;
    // we are done, give control back to caller of top_co.resume()
    } else {
        p.goal.top_co = {};
        return std::noop_coroutine();
    }
}

// When "returning" another coroutine, what happens is that
// we set it as our own continuation, thus once the final suspend
// happens, we transfer control to it.
// The original continuation we had is set as the continuation
// of the coroutine passed in.
// `final_suspend` is called after this, and `final_awaiter` will pass control off to `continuation`.
// However, we also have to transfer the ownership of `next`, since it's an rvalue,
// the handle to which is on our stack.
// We thus give it to our previous continuation.
void promise_type::return_value(Co&& next) {
    goal.trace("return_value(Co&&)");
    // we save our old continuation
    auto old_continuation = std::move(continuation);
    // we set our continuation to next
    continuation = std::move(next);
    // next must be continuation-less
    assert(!continuation->handle.promise().continuation);
    // next's continuation is set to the old continuation
    continuation->handle.promise().continuation = std::move(old_continuation);
}

// When we `co_await` another `Co`-returning coroutine,
// we tell the caller of `caller.resume()` to switch to our coroutine (`handle`).
// To make sure we return to the original coroutine, we set it as the continuation of our
// coroutine. In `final_awaiter` we check if it's set and if so we return to it.
//
// To explain in more understandable terms:
// When we `co_await Co_returning_function()`, this function is called on the resultant Co of
// the _called_ function, and C++ automatically passes the caller in.
// We don't use this caller, because we make use of the invariant that top_co == caller.
std::coroutine_handle<> Co::await_suspend(handle_type caller) {
    assert(handle); // we must be a valid coroutine
    auto& p = handle.promise();
    assert(!p.continuation); // we must have no continuation
    assert(p.goal.top_co); // top_co invariant must be maintained
    assert(p.goal.top_co->handle == caller); // top_co invariant must be maintained
    p.continuation = std::move(p.goal.top_co); // we set our continuation to be top_co (i.e. caller)
    p.goal.top_co = std::move(*this); // we set top_co to ourselves
    return handle; // we execute ourselves
}

void SuspendGoalAwaiter::await_suspend(std::coroutine_handle<> handle) noexcept {
}

PathSubstitutionGoal::PathSubstitutionGoal(const StorePath & storePath, Worker & worker, RepairFlag repair, std::optional<ContentAddress> ca)
    : Goal(worker, DerivedPath::Opaque { storePath })
    , storePath(storePath)
    , repair(repair)
    , top_co(init())
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


PathSubstitutionGoal::Done PathSubstitutionGoal::done(
    ExitCode result,
    BuildResult::Status status,
    std::optional<std::string> errorMsg)
{
    buildResult.status = status;
    if (errorMsg) {
        debug(*errorMsg);
        buildResult.errorMsg = *errorMsg;
    }
    amDone(result);
    return {};
}


void PathSubstitutionGoal::work()
{
    assert(top_co);
    assert(top_co->handle);
    assert(top_co->handle.promise().alive);
    top_co->handle.resume();
    // We either should be in a state where we can be work()-ed again,
    // or we should be done.
    assert(top_co || exitCode != ecBusy);
}


Co PathSubstitutionGoal::init()
{
    trace("init");

    worker.store.addTempRoot(storePath);

    /* If the path already exists we're done. */
    if (!repair && worker.store.isValidPath(storePath)) {
        co_return done(ecSuccess, BuildResult::AlreadyValid);
    }

    if (settings.readOnlyMode)
        throw Error("cannot substitute path '%s' - no write access to the Nix store", worker.store.printStorePath(storePath));

    subs = settings.useSubstitutes ? getDefaultSubstituters() : std::list<ref<Store>>();

    trace("calling tryNext");

    co_return tryNext();
}


Co PathSubstitutionGoal::tryNext()
{
    trace("trying next substituter");

    cleanup();

    if (subs.size() == 0) {
        /* None left.  Terminate this goal and let someone else deal
           with it. */

        if (substituterFailed) {
            worker.failedSubstitutions++;
            worker.updateProgress();
        }

        /* Hack: don't indicate failure if there were no substituters.
           In that case the calling derivation should just do a
           build. */
        co_return done(
            substituterFailed ? ecFailed : ecNoSubstituters,
            BuildResult::NoSubstituters,
            fmt("path '%s' is required, but there is no substituter that can build it", worker.store.printStorePath(storePath)));
    }

    sub = subs.front();
    subs.pop_front();

    if (ca) {
        subPath = sub->makeFixedOutputPathFromCA(
            std::string { storePath.name() },
            ContentAddressWithReferences::withoutRefs(*ca));
        if (sub->storeDir == worker.store.storeDir)
            assert(subPath == storePath);
    } else if (sub->storeDir != worker.store.storeDir) {
        co_return tryNext();
    }

    // Horrible, horrible code.
    // Needed because we can't `co_*` inside a catch-clause.
    // `std::variant` would be cleaner perhaps.
    int i = 0;
    std::optional<Error> e;
    try {
        // FIXME: make async
        info = sub->queryPathInfo(subPath ? *subPath : storePath);
    } catch (InvalidPath &) {
        i = 1;
    } catch (SubstituterDisabled & e_) {
        i = 2;
        e = e_;
    } catch (Error & e_) {
        i = 3;
        e = e_;
    }
    switch (i) {
        case 0:
            break;
        case 1:
            co_return tryNext();
        case 2:
            if (settings.tryFallback) {
                co_return tryNext();
            }
            throw *e;
        case 3:
            if (settings.tryFallback) {
                logError(e->info());
                co_return tryNext();
            }
            throw *e;
    }

    if (info->path != storePath) {
        if (info->isContentAddressed(*sub) && info->references.empty()) {
            auto info2 = std::make_shared<ValidPathInfo>(*info);
            info2->path = storePath;
            info = info2;
        } else {
            printError("asked '%s' for '%s' but got '%s'",
                sub->getUri(), worker.store.printStorePath(storePath), sub->printStorePath(info->path));
            co_return tryNext();
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
    if (!sub->isTrusted && worker.store.pathInfoIsUntrusted(*info))
    {
        warn("ignoring substitute for '%s' from '%s', as it's not signed by any of the keys in 'trusted-public-keys'",
            worker.store.printStorePath(storePath), sub->getUri());
        co_return tryNext();
    }

    /* To maintain the closure invariant, we first have to realise the
       paths referenced by this one. */
    for (auto & i : info->references)
        if (i != storePath) /* ignore self-references */
            addWaitee(worker.makePathSubstitutionGoal(i));

    if (!waitees.empty()) co_await SuspendGoal{};
    co_return referencesValid();
}


Co PathSubstitutionGoal::referencesValid()
{
    trace("all references realised");

    if (nrFailed > 0) {
        co_return done(
            nrNoSubstituters > 0 || nrIncompleteClosure > 0 ? ecIncompleteClosure : ecFailed,
            BuildResult::DependencyFailed,
            fmt("some references of path '%s' could not be realised", worker.store.printStorePath(storePath)));
    }

    for (auto & i : info->references)
        if (i != storePath) /* ignore self-references */
            assert(worker.store.isValidPath(i));

    worker.wakeUp(shared_from_this());
    co_await SuspendGoal{};
    co_return tryToRun();
}


Co PathSubstitutionGoal::tryToRun()
{
    trace("trying to run");

    /* Make sure that we are allowed to start a substitution.  Note that even
       if maxSubstitutionJobs == 0, we still allow a substituter to run. This
       prevents infinite waiting. */
    if (worker.getNrSubstitutions() >= std::max(1U, (unsigned int) settings.maxSubstitutionJobs)) {
        worker.waitForBuildSlot(shared_from_this());
        co_await SuspendGoal{};
    }

    maintainRunningSubstitutions = std::make_unique<MaintainCount<uint64_t>>(worker.runningSubstitutions);
    worker.updateProgress();

#ifndef _WIN32
    outPipe.create();
#else
    outPipe.createAsyncPipe(worker.ioport.get());
#endif

    promise = std::promise<void>();

    thr = std::thread([this]() {
        try {
            ReceiveInterrupts receiveInterrupts;

            /* Wake up the worker loop when we're done. */
            Finally updateStats([this]() { outPipe.writeSide.close(); });

            Activity act(*logger, actSubstitute, Logger::Fields{worker.store.printStorePath(storePath), sub->getUri()});
            PushActivity pact(act.id);

            copyStorePath(*sub, worker.store,
                subPath ? *subPath : storePath, repair, sub->isTrusted ? NoCheckSigs : CheckSigs);

            promise.set_value();
        } catch (...) {
            promise.set_exception(std::current_exception());
        }
    });

    worker.childStarted(shared_from_this(), {
#ifndef _WIN32
        outPipe.readSide.get()
#else
        &outPipe
#endif
    }, true, false);

    co_await SuspendGoal{};
    co_return finished();
}


Co PathSubstitutionGoal::finished()
{
    trace("substitute finished");

    thr.join();
    worker.childTerminated(this);

    try {
        promise.get_future().get();
    } catch (std::exception & e) {
        printError(e.what());

        /* Cause the parent build to fail unless --fallback is given,
           or the substitute has disappeared. The latter case behaves
           the same as the substitute never having existed in the
           first place. */
        try {
            throw;
        } catch (SubstituteGone &) {
        } catch (...) {
            substituterFailed = true;
        }

        /* Try the next substitute. */
        co_return tryNext();
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

    worker.doneNarSize += maintainExpectedNar->delta;
    maintainExpectedNar.reset();

    worker.updateProgress();

    co_return done(ecSuccess, BuildResult::Substituted);
}


void PathSubstitutionGoal::handleChildOutput(Descriptor fd, std::string_view data)
{
}


void PathSubstitutionGoal::handleEOF(Descriptor fd)
{
    if (fd == outPipe.readSide.get()) worker.wakeUp(shared_from_this());
}


void PathSubstitutionGoal::cleanup()
{
    try {
        if (thr.joinable()) {
            // FIXME: signal worker thread to quit.
            thr.join();
            worker.childTerminated(this);
        }

        outPipe.close();
    } catch (...) {
        ignoreException();
    }
}


}
