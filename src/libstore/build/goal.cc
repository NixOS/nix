#include "goal.hh"
#include "worker.hh"

namespace nix {


bool CompareGoalPtrs::operator() (const GoalPtr & a, const GoalPtr & b) const {
    std::string s1 = a->key();
    std::string s2 = b->key();
    return s1 < s2;
}


BuildResult Goal::getBuildResult(const DerivedPath & req) {
    BuildResult res { buildResult };

    if (auto pbp = std::get_if<DerivedPath::Built>(&req)) {
        auto & bp = *pbp;

        /* Because goals are in general shared between derived paths
           that share the same derivation, we need to filter their
           results to get back just the results we care about.
         */

        for (auto it = res.builtOutputs.begin(); it != res.builtOutputs.end();) {
            if (bp.outputs.contains(it->first.outputName))
                ++it;
            else
                it = res.builtOutputs.erase(it);
        }
    }

    return res;
}


void addToWeakGoals(WeakGoals & goals, GoalPtr p)
{
    if (goals.find(p) != goals.end())
        return;
    goals.insert(p);
}


void Goal::addWaitee(GoalPtr waitee)
{
    waitees.insert(waitee);
    addToWeakGoals(waitee->waiters, shared_from_this());
}


void Goal::waiteeDone(GoalPtr waitee, ExitCode result)
{
    assert(waitees.count(waitee));
    waitees.erase(waitee);

    trace(fmt("waitee '%s' done; %d left", waitee->name, waitees.size()));

    if (result == ecFailed || result == ecNoSubstituters || result == ecIncompleteClosure) ++nrFailed;

    if (result == ecNoSubstituters) ++nrNoSubstituters;

    if (result == ecIncompleteClosure) ++nrIncompleteClosure;

    if (waitees.empty() || (result == ecFailed && !settings.keepGoing)) {

        /* If we failed and keepGoing is not set, we remove all
           remaining waitees. */
        for (auto & goal : waitees) {
            goal->waiters.extract(shared_from_this());
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

    cleanup();
}


void Goal::trace(std::string_view s)
{
    debug("%1%: %2%", name, s);
}

}
