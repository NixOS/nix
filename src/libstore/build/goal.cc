#include "nix/store/build/goal.hh"
#include "nix/store/build/worker.hh"
#include "nix/store/worker-settings.hh"

#include <boost/asio/bind_cancellation_slot.hpp>
#include <boost/asio/steady_timer.hpp>

namespace nix {

void WorkerSettings::anchor() {}

TimedOut::TimedOut(time_t maxDuration)
    : CloneableError(BuildResult::Failure::TimedOut, "timed out after %1% seconds", maxDuration)
    , maxDuration(maxDuration)
{
}

void TimedOut::anchor() {}

void Goal::anchor() {}

static bool isCancellationException(std::exception_ptr ex)
{
    try {
        std::rethrow_exception(ex);
    } catch (const Interrupted &) {
        return true;
    } catch (const Cancelled &) {
        return true;
    } catch (const boost::system::system_error & e) {
        return e.code() == asio::error::operation_aborted;
    } catch (...) {
        return false;
    }
}

/* TODO: Probably move this to libutil or something. This should be used in much more places
   where a single exception might be rethrown and caught multiple times. */
[[noreturn]] static void rethrowMaybeCopyOfException(std::exception_ptr ex)
{
    try {
        std::rethrow_exception(ex);
    } catch (const nix::Error & e) {
        /* We have the minor annoyance of modifying exceptions when unwinding
           (for adding more context). This is the workaround. Could be useful
           for having more structured traces of failed builds. */
        e.throwClone();
    }
    /* Everything else just propagates. */
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

void Goal::maybeSpawnLazily()
{
    /* Launch work only once, and only when actually requested. */
    if (std::exchange(spawned, true))
        return;

    asio::co_spawn(
        worker.ex,
        run(),
        asio::bind_cancellation_slot(
            cancelSignal.slot(),
            [self = shared_from_this()](std::exception_ptr ex, ExitCode result) { self->finish(ex, result); }));
}

void Goal::finish(std::exception_ptr ex, ExitCode result)
{
    assert(!error);
    assert(exitCode == ecBusy);

    bool wasCancelled = ex && isCancellationException(ex);

    /* Stash the exception internally (unless cancelled) so that waiters are
       aware of it. */
    if (ex && !wasCancelled)
        error = ex;

    if (!ex) {
        assert(result == ecSuccess || result == ecFailed || result == ecNoSubstituters);
        exitCode = result;
        trace("done");
    }

    /* Log the failure if we have one and shouldn't preserve it.
       Only log for actual failures (ecFailed), not for ecNoSubstituters
       which indicates "couldn't substitute, will try building" - that's
       expected behavior, not an error. Top-level goals are not logged
       either: their failure is reported by whoever ran the worker. */
    if (const auto * failure = buildResult.tryGetFailure();
        exitCode == ecFailed && failure && !preserveFailure && !worker.topGoals.count(shared_from_this()))
        logError(failure->info());

    for (auto && waiter : std::exchange(waiters, {}))
        /* Would be nicer to return pass a borrowed BuildResult (or nullopt if cancelled) to the handlers. */
        asio::post(worker.ex, [completionHandler = std::move(waiter)]() mutable {
            (std::move(completionHandler))(boost::system::error_code{});
        });

    /* Clean up the weak pointer. */
    worker.removeGoal(shared_from_this());
}

asio::awaitable<void> Goal::join(GoalPtr goal)
{
    goal->maybeSpawnLazily();

    if (goal->exitCode != ecBusy)
        co_return;

    /* TODO: Maybe deduplicate this code somewhere? */
    if (goal->error) {
        assert(goal->isDone());
        rethrowMaybeCopyOfException(goal->error);
    }

    ++goal->numWaiters;

    Finally cleanup([&goal]() {
        if (--goal->numWaiters == 0 && goal->exitCode == ecBusy && !goal->error)
            goal->cancelSignal.emit(asio::cancellation_type::terminal);
    });

    auto cs = co_await asio::this_coro::cancellation_state;
    if (cs.cancelled() != asio::cancellation_type::none)
        throw boost::system::system_error(asio::error::operation_aborted);

    /* Initiate an async op that will complete once the awaited-for Goal completes or
       throws an exception. Awaiting will throw on cancellation or failures. */
    co_await asio::async_initiate<decltype(asio::use_awaitable), void(boost::system::error_code)>(
        [&](auto handler) {
            using Handler = std::decay_t<decltype(handler)>;
            auto state = std::make_shared<std::optional<Handler>>(std::move(handler));
            auto slot = asio::get_associated_cancellation_slot(**state);

            /* This is the completion handler that will be posted by the waitee upon
               completion. */
            goal->waiters.push_back([state](boost::system::error_code ec) {
                if (auto h = std::exchange(*state, std::nullopt))
                    (*std::move(h))(ec);
            });

            if (slot.is_connected())
                slot.assign([state, ex = goal->worker.ex](asio::cancellation_type) {
                    if (auto h = std::exchange(*state, std::nullopt))
                        asio::post(ex, [h = std::move(*h)]() mutable {
                            std::move(h)(boost::system::error_code(asio::error::operation_aborted));
                        });
                });
        },
        asio::use_awaitable);

    /* TODO: Maybe deduplicate this code somewhere? */
    if (goal->error) {
        assert(goal->isDone());
        rethrowMaybeCopyOfException(goal->error);
    }

    co_return;
}

asio::awaitable<void> Goal::join(Goals goals, bool keepGoing)
{
    if (goals.empty())
        co_return;

    auto ex = co_await asio::this_coro::executor;

    std::list<asio::cancellation_signal> cancelSignal;
    size_t pending = goals.size();
    std::exception_ptr error;

    co_await asio::async_initiate<decltype(asio::use_awaitable), void()>(
        [&](auto handler) {
            auto done = std::make_shared<decltype(handler)>(std::move(handler));

            /* Cancel all waitees if we are cancelled. */
            if (auto slot = asio::get_associated_cancellation_slot(*done); slot.is_connected())
                slot.assign([&cancelSignal](asio::cancellation_type ct) {
                    for (auto & s : cancelSignal)
                        s.emit(ct);
                });

            for (auto & goal : goals) {
                auto & stop = cancelSignal.emplace_back();
                asio::co_spawn(
                    ex,
                    join(goal),
                    asio::bind_cancellation_slot(
                        stop.slot(), [goal, &cancelSignal, &pending, &error, keepGoing, done](std::exception_ptr ex) {
                            /* Do nothing if we are just cancelled. */
                            bool realError = ex && !isCancellationException(ex);
                            if (realError && !error)
                                error = ex;
                            /* Without --keep-going, we cancel all other goals on first error. */
                            if (!keepGoing && (realError || goal->exitCode == ecFailed))
                                for (auto & s : cancelSignal)
                                    s.emit(asio::cancellation_type::terminal);
                            /* The last completion handler will complete the whole async op. */
                            if (--pending == 0)
                                (*done)();
                        }));
            }
        },
        asio::use_awaitable);

    if (error)
        rethrowMaybeCopyOfException(error);

    auto cs = co_await asio::this_coro::cancellation_state;
    if (cs.cancelled() != asio::cancellation_type::none)
        throw boost::system::system_error(asio::error::operation_aborted);
}

asio::awaitable<void> Goal::await(Goals waitees)
{
    co_await join(waitees, worker.settings.keepGoing);

    for (auto & w : waitees)
        switch (w->exitCode) {
        case ecFailed:
            ++nrFailed;
            break;
        case ecNoSubstituters:
            ++nrFailed;
            ++nrNoSubstituters;
            break;
        case ecBusy:
        case ecSuccess:
        default:
            break;
        }
}

asio::awaitable<void> Goal::waitForAWhile()
{
    trace("wait for a while");
    asio::steady_timer timer(co_await asio::this_coro::executor);
    timer.expires_after(std::chrono::seconds(worker.settings.pollInterval));
    co_await timer.async_wait(asio::use_awaitable);
}

void Goal::trace(std::string_view s)
{
    debug("%1%: %2%", name, s);
}

} // namespace nix
