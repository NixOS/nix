#pragma once

#include "lock.hh"
#include "store-api.hh"
#include "goal.hh"

namespace nix {

class Worker;

struct PathSubstitutionGoal : public Goal
{
    /* The store path that should be realised through a substitute. */
    StorePath storePath;

    /* The path the substituter refers to the path as. This will be
       different when the stores have different names. */
    std::optional<StorePath> subPath;

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

    typedef void (PathSubstitutionGoal::*GoalState)();
    GoalState state;

    /* Content address for recomputing store path */
    std::optional<ContentAddress> ca;

    void done(
        ExitCode result,
        BuildResult::Status status,
        std::optional<std::string> errorMsg = {});

public:
    PathSubstitutionGoal(const StorePath & storePath, Worker & worker, RepairFlag repair = NoRepair, std::optional<ContentAddress> ca = std::nullopt);
    ~PathSubstitutionGoal();

    void timedOut(Error && ex) override { abort(); };

    std::string key() override
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
    void handleChildOutput(int fd, std::string_view data) override;
    void handleEOF(int fd) override;

    void cleanup() override;
};

}
