#pragma once

#include "worker.hh"
#include "lock.hh"
#include "store-api.hh"
#include "goal.hh"
#include "drv-output-info.hh"

namespace nix {

class Worker;

class SubstitutionGoal : public Goal
{
public:
    SubstitutionGoal(Worker&, RepairFlag);
    ~SubstitutionGoal();

    /* The states. */
    void init();
    virtual void tryNext() = 0;
    virtual void referencesValid() = 0;
    virtual void tryToRun() = 0;
    void finished();

    typedef void (SubstitutionGoal::*GoalState)();
    GoalState state;

    void work() override;

    /* Callback used by the worker to write to the log. */
    void handleChildOutput(int fd, const string & data) override;
    void handleEOF(int fd) override;

    virtual DrvInput getTarget() const = 0;

protected:
    friend class Worker;

    /* Pipe for the substituter's standard output. */
    Pipe outPipe;

    /* The substituter thread. */
    std::thread thr;

    /* The remaining substituters. */
    std::list<ref<Store>> subs;

    /* The current substituter. */
    std::shared_ptr<Store> sub;

    /* Whether a substituter failed. */
    bool substituterFailed = false;

    std::unique_ptr<MaintainCount<uint64_t>> maintainExpectedSubstitutions,
        maintainRunningSubstitutions, maintainExpectedNar, maintainExpectedDownload;

    /* Whether to try to repair a valid path. */
    RepairFlag repair;

    /* The final store path as it is known by the local and the remote path */
    std::optional<StorePath> locallyKnownPath, remotelyKnownPath;

    std::promise<void> promise;

    /* Path info returned by the substituter's query info operation. */
    std::shared_ptr<const ValidPathInfo> info;
};

std::shared_ptr<SubstitutionGoal> makeSubstitutionGoal(
    const StorePath& storePath,
    Worker& worker,
    RepairFlag repair = NoRepair,
    std::optional<ContentAddress> ca = std::nullopt);

}
