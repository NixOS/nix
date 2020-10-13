#pragma once

#include "lock.hh"
#include "store-api.hh"
#include "goal.hh"

namespace nix {

class Worker;

class SubstitutionGoal : public Goal
{
    friend class Worker;

private:
    /* The store path that should be realised through a substitute. */
    // FIXME OwnedStorePathOrDesc storePath
    StorePath storePath;

    /* The remaining substituters. */
    std::list<ref<Store>> subs;

    /* The current substituter. */
    std::shared_ptr<Store> sub;

    /* Whether a substituter failed. */
    bool substituterFailed = false;

    /* Path info returned by the substituter's query info operation. */
    std::shared_ptr<const ValidPathInfo> info;

    /* Pipe for the substituter's standard output. */
    Pipe outPipe;

    /* The substituter thread. */
    std::thread thr;

    std::promise<void> promise;

    /* Whether to try to repair a valid path. */
    RepairFlag repair;

    /* Location where we're downloading the substitute.  Differs from
       storePath when doing a repair. */
    Path destPath;

    std::unique_ptr<MaintainCount<uint64_t>> maintainExpectedSubstitutions,
        maintainRunningSubstitutions, maintainExpectedNar, maintainExpectedDownload;

    typedef void (SubstitutionGoal::*GoalState)();
    GoalState state;

    /* Content address for recomputing store path */
    // TODO delete once `storePath` is variant.
    std::optional<StorePathDescriptor> ca;

public:
    SubstitutionGoal(const StorePath & storePath, Worker & worker, RepairFlag repair = NoRepair, std::optional<StorePathDescriptor> ca = std::nullopt);
    ~SubstitutionGoal();

    void timedOut(Error && ex) override { abort(); };

    string key() override
    {
        /* "a$" ensures substitution goals happen before derivation
           goals. */
        return "a$" + std::string(storePath.name()) + "$" + worker.store.printStorePath(storePath);
    }

    void work() override;

    /* The states. */
    void init();
    void tryNext();
    void gotInfo();
    void referencesValid();
    void tryToRun();
    void finished();

    /* Callback used by the worker to write to the log. */
    void handleChildOutput(int fd, const string & data) override;
    void handleEOF(int fd) override;

    StorePath getStorePath() { return storePath; }
};

}
