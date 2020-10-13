#include "goal.hh"
#include "worker.hh"

namespace nix {


bool CompareGoalPtrs::operator() (const GoalPtr & a, const GoalPtr & b) const {
    string s1 = a->key();
    string s2 = b->key();
    return s1 < s2;
}


void addToWeakGoals(WeakGoals & goals, GoalPtr p)
{
    // FIXME: necessary?
    // FIXME: O(n)
    for (auto & i : goals)
        if (i.lock() == p) return;
    goals.push_back(p);
}


void Goal::addWaitee(GoalPtr waitee)
{
    waitees.insert(waitee);
    addToWeakGoals(waitee->waiters, shared_from_this());
}


void Goal::waiteeDone(GoalPtr waitee, ExitCode result)
{
    assert(waitees.find(waitee) != waitees.end());
    waitees.erase(waitee);

    trace(fmt("waitee '%s' done; %d left", waitee->name, waitees.size()));

    if (result == ecFailed || result == ecNoSubstituters || result == ecIncompleteClosure) ++nrFailed;

    if (result == ecNoSubstituters) ++nrNoSubstituters;

    if (result == ecIncompleteClosure) ++nrIncompleteClosure;

    if (waitees.empty() || (result == ecFailed && !settings.keepGoing)) {

        /* If we failed and keepGoing is not set, we remove all
           remaining waitees. */
        for (auto & goal : waitees) {
            WeakGoals waiters2;
            for (auto & j : goal->waiters)
                if (j.lock() != shared_from_this()) waiters2.push_back(j);
            goal->waiters = waiters2;
        }
        waitees.clear();

        worker.wakeUp(shared_from_this());
    }
}


void Goal::amDone(ExitCode result, std::optional<Error> ex)
{
    trace("done");
    assert(exitCode == ecBusy);
    assert(result == ecSuccess || result == ecFailed || result == ecNoSubstituters || result == ecIncompleteClosure);
    exitCode = result;

    if (ex) {
        if (!waiters.empty())
            logError(ex->info());
        else
            this->ex = std::move(*ex);
    }

    for (auto & i : waiters) {
        GoalPtr goal = i.lock();
        if (goal) goal->waiteeDone(shared_from_this(), result);
    }
    waiters.clear();
    worker.removeGoal(shared_from_this());
}


void Goal::trace(const FormatOrString & fs)
{
    debug("%1%: %2%", name, fs.s);
}

}
