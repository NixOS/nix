#include "nix/store/build/goal.hh"
#include "nix/store/build/worker.hh"
#include "nix/store/worker-settings.hh"

namespace nix {

Goal::Goal(Worker & worker, Co<ExitCode> init)
    : worker(worker)
    , top_co(std::move(init))
{
    // top_co shouldn't have a goal already, should be nullptr.
    auto handle = HandleType<ExitCode>::from_address(top_co->handle.address());
    assert(!handle.promise().goal);
    // we set it such that top_co can pass it down to its subcoroutines.
    handle.promise().goal = this;
    // Nobody awaits the top-level coroutine, so its result goes to us.
    handle.promise().resultSlot = &finalExitCode;
}

void WorkerSettings::anchor() {}

TimedOut::TimedOut(time_t maxDuration)
    : CloneableError(BuildResult::Failure::TimedOut, "timed out after %1% seconds", maxDuration)
    , maxDuration(maxDuration)
{
}

void TimedOut::anchor() {}

void Goal::anchor() {}

void Goal::ChildEvents::pushChildEvent(ChildOutput event)
{
    if (childTimeout)
        return; // Already timed out, ignore
    childOutputs.push(std::move(event));
}

void Goal::ChildEvents::pushChildEvent(ChildEOF event)
{
    if (childTimeout)
        return; // Already timed out, ignore
    assert(!childEOF);
    childEOF = std::move(event);
}

void Goal::ChildEvents::pushChildEvent(TimedOut event)
{
    // Timeout is immediate - flush pending events
    childOutputs = {};
    childEOF.reset();
    childTimeout = std::make_unique<TimedOut>(std::move(event));
}

bool Goal::ChildEvents::hasChildEvent() const
{
    return !childOutputs.empty() || childEOF || childTimeout;
}

Goal::ChildEvent Goal::ChildEvents::popChildEvent()
{
    if (!childOutputs.empty()) {
        auto event = std::move(childOutputs.front());
        childOutputs.pop();
        return event;
    }
    if (childEOF)
        return *std::exchange(childEOF, std::nullopt);
    if (childTimeout)
        return std::exchange(childTimeout, nullptr);
    unreachable();
}

using Suspend = nix::Goal::Suspend;

Goal::CoBase & Goal::CoBase::operator=(CoBase && rhs) noexcept
{
    if (this == &rhs)
        return *this;
    if (handle) {
        auto baseHandle = HandleTypeBase::from_address(handle.address());
        baseHandle.promise().alive = false;
        handle.destroy();
    }
    handle = std::exchange(rhs.handle, nullptr);
    return *this;
}

Goal::CoBase::~CoBase()
{
    if (handle) {
        auto baseHandle = HandleTypeBase::from_address(handle.address());
        baseHandle.promise().alive = false;
        handle.destroy();
    }
}

bool CompareGoalPtrs::operator()(const GoalPtr & a, const GoalPtr & b) const
{
    return a->keyCached() < b->keyCached();
}

void addToWeakGoals(WeakGoals & goals, GoalPtr p)
{
    if (goals.find(p) != goals.end())
        return;
    goals.insert(p);
}

Goal::Co<void> Goal::await(Goals new_waitees)
{
    assert(waitees.empty());
    if (!new_waitees.empty()) {
        waitees = std::move(new_waitees);
        for (auto waitee : waitees) {
            addToWeakGoals(waitee->waiters, shared_from_this());
        }
        co_await Suspend{};
        assert(waitees.empty());
    }
}

void Goal::trace(std::string_view s)
{
    debug("%1%: %2%", name, s);
}

void Goal::work()
{
    assert(top_co);
    assert(top_co->handle);
    auto baseHandle = HandleTypeBase::from_address(top_co->handle.address());
    assert(baseHandle.promise().alive);
    baseHandle.resume();

    /* Still suspended somewhere, so we'll be work()-ed again. */
    if (top_co)
        return;

    trace("done");
    assert(finalExitCode);
    assert(exitCode == ecBusy);
    auto result = *std::exchange(finalExitCode, std::nullopt);
    assert(result == ecSuccess || result == ecFailed || result == ecNoSubstituters);
    exitCode = result;

    // Log the failure if we have one and shouldn't preserve it.
    // Only log for actual failures (ecFailed), not for ecNoSubstituters
    // which indicates "couldn't substitute, will try building" - that's
    // expected behavior, not an error.
    if (result == ecFailed) {
        if (auto * failure = buildResult.tryGetFailure()) {
            if (!preserveFailure && !waiters.empty())
                logError(failure->info());
        }
    }

    for (auto & i : waiters) {
        GoalPtr goal = i.lock();
        if (goal) {
            auto me = shared_from_this();
            assert(goal->waitees.count(me));
            goal->waitees.erase(me);

            goal->trace(fmt("waitee '%s' done; %d left", name, goal->waitees.size()));

            if (result == ecFailed || result == ecNoSubstituters)
                ++goal->nrFailed;

            if (result == ecNoSubstituters)
                ++goal->nrNoSubstituters;

            if (goal->waitees.empty()) {
                worker.wakeUp(goal);
            } else if (result == ecFailed && !worker.settings.keepGoing) {
                /* If we failed and keepGoing is not set, we remove all
                   remaining waitees. */
                for (auto & g : goal->waitees) {
                    g->waiters.erase(goal);
                }
                goal->waitees.clear();

                worker.wakeUp(goal);
            }
        }
    }
    waiters.clear();
    worker.removeGoal(shared_from_this());

    cleanup();
}

void Goal::handleChildOutput(Descriptor fd, std::string_view data)
{
    childEvents.pushChildEvent(ChildOutput{fd, std::string{data}});
    worker.wakeUp(shared_from_this());
}

void Goal::handleEOF(Descriptor fd)
{
    childEvents.pushChildEvent(ChildEOF{fd});
    worker.wakeUp(shared_from_this());
}

void Goal::timedOut(TimedOut && ex)
{
    childEvents.pushChildEvent(std::move(ex));
    worker.wakeUp(shared_from_this());
}

Goal::Co<void> Goal::yield()
{
    worker.wakeUp(shared_from_this());
    co_await Suspend{};
}

Goal::Co<void> Goal::waitForAWhile()
{
    worker.waitForAWhile(shared_from_this());
    co_await Suspend{};
}

Goal::Co<void> Goal::waitUntilWoken()
{
    worker.waitForCompletion(shared_from_this());
    co_await Suspend{};
}

Goal::Co<void> Goal::waitForBuildSlot()
{
    worker.waitForBuildSlot(shared_from_this());
    co_await Suspend{};
}

} // namespace nix
