#pragma once

#include "types.hh"
#include "store-api.hh"
#include "build-result.hh"

namespace nix {

/* Forward definition. */
struct Goal;
class Worker;

/* A pointer to a goal. */
typedef std::shared_ptr<Goal> GoalPtr;
typedef std::weak_ptr<Goal> WeakGoalPtr;

struct CompareGoalPtrs {
    bool operator() (const GoalPtr & a, const GoalPtr & b) const;
};

/* Set of goals. */
typedef std::set<GoalPtr, CompareGoalPtrs> Goals;
typedef std::set<WeakGoalPtr, std::owner_less<WeakGoalPtr>> WeakGoals;

/* A map of paths to goals (and the other way around). */
typedef std::map<StorePath, WeakGoalPtr> WeakGoalMap;

struct Goal : public std::enable_shared_from_this<Goal>
{
    typedef enum {ecBusy, ecSuccess, ecFailed, ecNoSubstituters, ecIncompleteClosure} ExitCode;

    /* Backlink to the worker. */
    Worker & worker;

    /* Goals that this goal is waiting for. */
    Goals waitees;

    /* Goals waiting for this one to finish.  Must use weak pointers
       here to prevent cycles. */
    WeakGoals waiters;

    /* Number of goals we are/were waiting for that have failed. */
    size_t nrFailed = 0;

    /* Number of substitution goals we are/were waiting for that
       failed because there are no substituters. */
    size_t nrNoSubstituters = 0;

    /* Number of substitution goals we are/were waiting for that
       failed because they had unsubstitutable references. */
    size_t nrIncompleteClosure = 0;

    /* Name of this goal for debugging purposes. */
    std::string name;

    /* Whether the goal is finished. */
    ExitCode exitCode = ecBusy;

    /* Build result. */
    BuildResult buildResult;

    /* Exception containing an error message, if any. */
    std::optional<Error> ex;

    Goal(Worker & worker, DerivedPath path)
        : worker(worker)
        , buildResult { .path = std::move(path) }
    { }

    virtual ~Goal()
    {
        trace("goal destroyed");
    }

    virtual void work() = 0;

    void addWaitee(GoalPtr waitee);

    virtual void waiteeDone(GoalPtr waitee, ExitCode result);

    virtual void handleChildOutput(int fd, std::string_view data)
    {
        abort();
    }

    virtual void handleEOF(int fd)
    {
        abort();
    }

    void trace(const FormatOrString & fs);

    std::string getName()
    {
        return name;
    }

    /* Callback in case of a timeout.  It should wake up its waiters,
       get rid of any running child processes that are being monitored
       by the worker (important!), etc. */
    virtual void timedOut(Error && ex) = 0;

    virtual std::string key() = 0;

    void amDone(ExitCode result, std::optional<Error> ex = {});

    virtual void cleanup() { }
};

void addToWeakGoals(WeakGoals & goals, GoalPtr p);

}
