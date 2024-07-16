#include "goal.hh"
#include "worker.hh"

namespace nix {

using Co = nix::Goal::Co;
using promise_type = nix::Goal::promise_type;
using handle_type = nix::Goal::handle_type;
using Suspend = nix::Goal::Suspend;

Co::Co(Co&& rhs) {
    this->handle = rhs.handle;
    rhs.handle = nullptr;
}
void Co::operator=(Co&& rhs) {
    this->handle = rhs.handle;
    rhs.handle = nullptr;
}
Co::~Co() {
    if (handle) {
        handle.promise().alive = false;
        handle.destroy();
    }
}

Co promise_type::get_return_object() {
    auto handle = handle_type::from_promise(*this);
    return Co{handle};
};

std::coroutine_handle<> promise_type::final_awaiter::await_suspend(handle_type h) noexcept {
    auto& p = h.promise();
    auto goal = p.goal;
    assert(goal);
    goal->trace("in final_awaiter");
    auto c = std::move(p.continuation);

    if (c) {
        // We still have a continuation, i.e. work to do.
        // We assert that the goal is still busy.
        assert(goal->exitCode == ecBusy);
        assert(goal->top_co); // Goal must have an active coroutine.
        assert(goal->top_co->handle == h); // The active coroutine must be us.
        assert(p.alive); // We must not have been destructed.

        // we move continuation to the top,
        // note: previous top_co is actually h, so by moving into it,
        // we're calling the destructor on h, DON'T use h and p after this!

        // We move our continuation into `top_co`, i.e. the marker for the active continuation.
        // By doing this we destruct the old `top_co`, i.e. us, so `h` can't be used anymore.
        // Be careful not to access freed memory!
        goal->top_co = std::move(c);

        // We resume `top_co`.
        return goal->top_co->handle;
    } else {
        // We have no continuation, i.e. no more work to do,
        // so the goal must not be busy anymore.
        assert(goal->exitCode != ecBusy);

        // We reset `top_co` for good measure.
        p.goal->top_co = {};

        // We jump to the noop coroutine, which doesn't do anything and immediately suspends.
        // This passes control back to the caller of goal.work().
        return std::noop_coroutine();
    }
}

void promise_type::return_value(Co&& next) {
    goal->trace("return_value(Co&&)");
    // Save old continuation.
    auto old_continuation = std::move(continuation);
    // We set next as our continuation.
    continuation = std::move(next);
    // We set next's goal, and thus it must not have one already.
    assert(!continuation->handle.promise().goal);
    continuation->handle.promise().goal = goal;
    // Nor can next have a continuation, as we set it to our old one.
    assert(!continuation->handle.promise().continuation);
    continuation->handle.promise().continuation = std::move(old_continuation);
}

std::coroutine_handle<> nix::Goal::Co::await_suspend(handle_type caller) {
    assert(handle); // we must be a valid coroutine
    auto& p = handle.promise();
    assert(!p.continuation); // we must have no continuation
    assert(!p.goal); // we must not have a goal yet
    auto goal = caller.promise().goal;
    assert(goal);
    p.goal = goal;
    p.continuation = std::move(goal->top_co); // we set our continuation to be top_co (i.e. caller)
    goal->top_co = std::move(*this); // we set top_co to ourselves, don't use this anymore after this!
    return p.goal->top_co->handle; // we execute ourselves
}

bool CompareGoalPtrs::operator() (const GoalPtr & a, const GoalPtr & b) const {
    std::string s1 = a->key();
    std::string s2 = b->key();
    return s1 < s2;
}


BuildResult Goal::getBuildResult(const DerivedPath & req) const {
    BuildResult res { buildResult };

    if (auto pbp = std::get_if<DerivedPath::Built>(&req)) {
        auto & bp = *pbp;

        /* Because goals are in general shared between derived paths
           that share the same derivation, we need to filter their
           results to get back just the results we care about.
         */

        for (auto it = res.builtOutputs.begin(); it != res.builtOutputs.end();) {
            if (bp.outputs.contains(it->first))
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

Goal::Done Goal::amDone(ExitCode result, std::optional<Error> ex)
{
    trace("done");
    assert(top_co);
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

    // We drop the continuation.
    // In `final_awaiter` this will signal that there is no more work to be done.
    top_co->handle.promise().continuation = {};

    // won't return to caller because of logic in final_awaiter
    return Done{};
}


void Goal::trace(std::string_view s)
{
    debug("%1%: %2%", name, s);
}

void Goal::work()
{
    assert(top_co);
    assert(top_co->handle);
    assert(top_co->handle.promise().alive);
    top_co->handle.resume();
    // We either should be in a state where we can be work()-ed again,
    // or we should be done.
    assert(top_co || exitCode != ecBusy);
}


}
