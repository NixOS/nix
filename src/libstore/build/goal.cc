#include "goal.hh"
#include "worker.hh"

namespace nix {

using Co = nix::Goal::Co;
using promise_type = nix::Goal::promise_type;
using handle_type = nix::Goal::handle_type;
using SuspendGoal = nix::Goal::SuspendGoal;

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
    assert(p.goal);
    p.goal->trace("in final_awaiter");
    // we are still on-going
    if (p.goal->exitCode == ecBusy) {
        p.goal->trace("we're busy");
        assert(p.alive); // sanity check to make sure it's not been destructed prematurely
        assert(p.goal->top_co);
        assert(p.goal->top_co->handle == h);
        // we move continuation to the top,
        // note: previous top_co is actually h, so by moving into it,
        // we're calling the destructor on h, DON'T use h and p after this!
        auto c = std::move(p.continuation);
        assert(c);
        auto& goal = p.goal;
        goal->top_co = std::move(c);
        return goal->top_co->handle;
    // we are done, give control back to caller of top_co.resume()
    } else {
        p.goal->top_co = {};
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
    goal->trace("return_value(Co&&)");
    // we save our old continuation
    auto old_continuation = std::move(continuation);
    // we set our continuation to next
    continuation = std::move(next);
    // next must not have a goal already
    assert(!continuation->handle.promise().goal);
    // next's goal is set to our goal
    continuation->handle.promise().goal = goal;
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

Co Goal::amDone(ExitCode result, std::optional<Error> ex)
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

    // won't return to caller because of logic in final_awaiter
    co_return Return{};
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
