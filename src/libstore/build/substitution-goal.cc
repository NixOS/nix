#include "worker.hh"
#include "substitution-goal.hh"
#include "nar-info.hh"
#include "finally.hh"

namespace nix {

SubstitutionGoal::SubstitutionGoal(Worker& worker, RepairFlag repair)
    : Goal(worker)
    , repair(repair)
{
    state = &SubstitutionGoal::init;
    maintainExpectedSubstitutions = std::make_unique<MaintainCount<uint64_t>>(worker.expectedSubstitutions);
}

SubstitutionGoal::~SubstitutionGoal()
{
    try {
        if (thr.joinable()) {
            // FIXME: signal worker thread to quit.
            thr.join();
            worker.childTerminated(this);
        }
    } catch (...) {
        ignoreException();
    }
}


void SubstitutionGoal::work()
{
    (this->*state)();
}

void SubstitutionGoal::init()
{
    trace("init");

    if (locallyKnownPath) {
        worker.store.addTempRoot(*locallyKnownPath);

        /* If the path already exists we're done. */
        if (!repair && worker.store.isValidPath(*locallyKnownPath)) {
            amDone(ecSuccess);
            return;
        }
    }

    if (settings.readOnlyMode)
        throw Error("cannot substitute path '%s' - no write access to the Nix store", getTarget().to_string());

    subs = settings.useSubstitutes ? getDefaultSubstituters() : std::list<ref<Store>>();

    tryNext();
}





void SubstitutionGoal::finished()
{
    assert(locallyKnownPath);
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
        state = &SubstitutionGoal::tryNext;
        worker.wakeUp(shared_from_this());
        return;
    }

    worker.markContentsGood(*locallyKnownPath);

    printMsg(lvlChatty, "substitution of path '%s' succeeded", worker.store.printStorePath(*locallyKnownPath));

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

    amDone(ecSuccess);
}


void SubstitutionGoal::handleChildOutput(int fd, const string & data)
{
}


void SubstitutionGoal::handleEOF(int fd)
{
    if (fd == outPipe.readSide.get()) worker.wakeUp(shared_from_this());
}

}
