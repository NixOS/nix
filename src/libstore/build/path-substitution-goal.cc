#include "substitution-goal.hh"
#include "nar-info.hh"
#include "finally.hh"

namespace nix {

class PathSubstitutionGoal : public SubstitutionGoal
{
private:
    friend class Worker;

    /* The store path that should be realised through a substitute. */
    StorePath storePath;

    /* Location where we're downloading the substitute.  Differs from
       storePath when doing a repair. */
    Path destPath;

    /* Content address for recomputing store path */
    std::optional<ContentAddress> ca;

public:
    PathSubstitutionGoal(const StorePath & storePath, Worker & worker, RepairFlag repair = NoRepair, std::optional<ContentAddress> ca = std::nullopt);

    void timedOut(Error && ex) override { abort(); };

    string key() override
    {
        /* "a$" ensures substitution goals happen before derivation
           goals. */
        return "a$" + std::string(storePath.name()) + "$" + worker.store.printStorePath(storePath);
    }

    /* The states. */
    void tryNext() override;
    void referencesValid() override;
    void tryToRun() override;

    DrvInput getTarget() const override { return storePath; }
};

PathSubstitutionGoal::PathSubstitutionGoal(const StorePath & storePath, Worker & worker, RepairFlag repair, std::optional<ContentAddress> ca)
    : SubstitutionGoal(worker, repair)
    , storePath(storePath)
    , ca(ca)
{
    name = fmt("substitution of '%s'", worker.store.printStorePath(this->storePath));
    locallyKnownPath = storePath;
    trace("created");
}

void PathSubstitutionGoal::tryNext()
{
    trace("trying next substituter");

    if (subs.size() == 0) {
        /* None left.  Terminate this goal and let someone else deal
           with it. */
        debug("path '%s' is required, but there is no substituter that can build it", worker.store.printStorePath(storePath));

        /* Hack: don't indicate failure if there were no substituters.
           In that case the calling derivation should just do a
           build. */
        amDone(substituterFailed ? ecFailed : ecNoSubstituters);

        if (substituterFailed) {
            worker.failedSubstitutions++;
            worker.updateProgress();
        }

        return;
    }

    sub = subs.front();
    subs.pop_front();

    if (ca) {
        remotelyKnownPath = sub->makeFixedOutputPathFromCA(storePath.name(), *ca);
        if (sub->storeDir == worker.store.storeDir)
            assert(remotelyKnownPath == storePath);
    } else if (sub->storeDir != worker.store.storeDir) {
        tryNext();
        return;
    }

    try {
        // FIXME: make async
        info = sub->queryPathInfo(remotelyKnownPath ? *remotelyKnownPath : storePath);
    } catch (InvalidPath &) {
        tryNext();
        return;
    } catch (SubstituterDisabled &) {
        if (settings.tryFallback) {
            tryNext();
            return;
        }
        throw;
    } catch (Error & e) {
        if (settings.tryFallback) {
            logError(e.info());
            tryNext();
            return;
        }
        throw;
    }

    if (info->path != storePath) {
        if (info->isContentAddressed(*sub) && info->references.empty()) {
            auto info2 = std::make_shared<ValidPathInfo>(*info);
            info2->path = storePath;
            info = info2;
        } else {
            printError("asked '%s' for '%s' but got '%s'",
                sub->getUri(), worker.store.printStorePath(storePath), sub->printStorePath(info->path));
            tryNext();
            return;
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
    if (worker.store.requireSigs
        && !sub->isTrusted
        && !info->checkSignatures(worker.store, worker.store.getPublicKeys()))
    {
        logWarning({
            .name = "Invalid path signature",
            .hint = hintfmt("substituter '%s' does not have a valid signature for path '%s'",
                sub->getUri(), worker.store.printStorePath(storePath))
        });
        tryNext();
        return;
    }

    /* To maintain the closure invariant, we first have to realise the
       paths referenced by this one. */
    for (auto & i : info->references)
        if (i != storePath) /* ignore self-references */
            addWaitee(worker.makeSubstitutionGoal(i));

    if (waitees.empty()) /* to prevent hang (no wake-up event) */
        referencesValid();
    else
        state = &SubstitutionGoal::referencesValid;
}

void PathSubstitutionGoal::referencesValid()
{
    trace("all references realised");

    if (nrFailed > 0) {
        debug("some references of path '%s' could not be realised", worker.store.printStorePath(storePath));
        amDone(nrNoSubstituters > 0 || nrIncompleteClosure > 0 ? ecIncompleteClosure : ecFailed);
        return;
    }

    for (auto & i : info->references)
        if (i != storePath) /* ignore self-references */
            assert(worker.store.isValidPath(i));

    state = &SubstitutionGoal::tryToRun;
    worker.wakeUp(shared_from_this());
}


void PathSubstitutionGoal::tryToRun()
{
    trace("trying to run");

    /* Make sure that we are allowed to start a build.  Note that even
       if maxBuildJobs == 0 (no local builds allowed), we still allow
       a substituter to run.  This is because substitutions cannot be
       distributed to another machine via the build hook. */
    if (worker.getNrLocalBuilds() >= std::max(1U, (unsigned int) settings.maxBuildJobs)) {
        worker.waitForBuildSlot(shared_from_this());
        return;
    }

    maintainRunningSubstitutions = std::make_unique<MaintainCount<uint64_t>>(worker.runningSubstitutions);
    worker.updateProgress();

    outPipe.create();

    promise = std::promise<void>();

    thr = std::thread([this]() {
        try {
            /* Wake up the worker loop when we're done. */
            Finally updateStats([this]() { outPipe.writeSide = -1; });

            Activity act(*logger, actSubstitute, Logger::Fields{worker.store.printStorePath(storePath), sub->getUri()});
            PushActivity pact(act.id);

            copyStorePath(ref<Store>(sub), ref<Store>(worker.store.shared_from_this()),
                remotelyKnownPath ? *remotelyKnownPath : storePath, repair, sub->isTrusted ? NoCheckSigs : CheckSigs);

            promise.set_value();
        } catch (...) {
            promise.set_exception(std::current_exception());
        }
    });

    worker.childStarted(shared_from_this(), {outPipe.readSide.get()}, true, false);

    state = &SubstitutionGoal::finished;
}

std::shared_ptr<SubstitutionGoal> makeSubstitutionGoal(const StorePath & storePath, Worker & worker, RepairFlag repair, std::optional<ContentAddress> ca)
{
    return std::make_shared<PathSubstitutionGoal>(storePath, worker, repair, ca);
}
}
