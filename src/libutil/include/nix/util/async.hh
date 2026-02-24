#pragma once
///@file

#include "nix/util/callback.hh"
#include "nix/util/ref.hh"
#include "nix/util/signals.hh"

#include <boost/asio/io_context.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/awaitable.hpp>
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
                    /* Early return for cooperative cancellation. The callback has been caller
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

} // namespace nix
