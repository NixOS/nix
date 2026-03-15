#pragma once
///@file

#include "nix/util/callback.hh"
#include "nix/util/ref.hh"
#include "nix/util/signals.hh"

#include <boost/asio/bind_executor.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/associated_cancellation_slot.hpp>

#include <concepts>

namespace nix {

namespace asio = boost::asio;

template<typename T, std::invocable<Callback<T>> F, typename CompletionToken>
auto callbackToAwaitable(F && initiate, CompletionToken && token)
{
    return asio::async_initiate<CompletionToken, void(std::future<T>)>(
        [initiate = std::forward<F>(initiate)](auto handler) mutable {
            auto executor = asio::get_associated_executor(handler);
            auto done = std::make_shared<std::atomic<bool>>(false);
            auto h = std::make_shared<decltype(handler)>(std::move(handler));

            if (auto slot = asio::get_associated_cancellation_slot(*h); slot.is_connected()) {
                std::weak_ptr wh = h; /* To handle the cyclic ownership. */
                std::weak_ptr wdone = done;
                slot.assign([executor, wh, wdone](asio::cancellation_type /*don't care*/) {
                    auto h = wh.lock();
                    auto done = wdone.lock();
                    if (!h || !done || done->exchange(true))
                        return; /* Gracefully die. */
                    /* Doesn't need to be kept alive for get_future() since it shares the ownership. */
                    std::promise<T> p;
                    p.set_exception(std::make_exception_ptr(Interrupted("interrupted by user")));
                    asio::post(executor, [h, fut = p.get_future()]() mutable { std::move (*h)(std::move(fut)); });
                });
            }

            initiate(Callback<T>([executor, done, h](std::future<T> fut) mutable {
                if (done->exchange(true))
                    /* Early return for cooperative cancellation. The callback has been called
                       later than we've been cancelled. In practice we'll get an error, the handler
                       has already been posted by the cancellation handler. */
                    return;
                asio::post(executor, [h, fut = std::move(fut)]() mutable { std::move (*h)(std::move(fut)); });
            }));
        },
        std::forward<CompletionToken>(token));
}

/**
 * Convert a completion handler callback into a stackless coroutine. The
 * callback can be invoked on any thread and the completion handler will be
 * marshalled to the coroutines executer.
 */
template<typename T, std::invocable<Callback<T>> F>
asio::awaitable<T> callbackToAwaitable(F && initiate)
{
    auto fut = co_await callbackToAwaitable<T>(std::forward<F>(initiate), asio::use_awaitable);
    co_return fut.get();
}

template<typename Range, typename F>
asio::awaitable<void> forEachAsync(Range && range, F && f)
{
    auto executor = co_await asio::this_coro::executor;
    auto strand = asio::make_strand(executor); /* Serialise on a single strand. No synchronisation is needed. */
    auto guard = asio::make_work_guard(executor);
    std::size_t pending = 0;
    std::exception_ptr err;

    for (auto && elt : range) {
        ++pending;
        asio::co_spawn(strand, f(elt), asio::bind_executor(strand, [&](std::exception_ptr ex) {
                           /* First exception wins. Other ones get swallowed. */
                           if (ex && !err)
                               err = ex;
                           --pending;
                       }));
    }

    while (pending > 0)
        co_await asio::post(strand, asio::use_awaitable);

    guard.reset();

    if (err)
        std::rethrow_exception(err);
}

} // namespace nix
