#pragma once
///@file

#include "nix/util/async.hh"
#include "nix/util/ref.hh"

#include <boost/asio/io_context.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/bind_executor.hpp>
#include <boost/asio/executor_work_guard.hpp>

#include <queue>
#include <set>

namespace nix {

template<typename T>
using GetEdgesAsync = std::function<asio::awaitable<std::set<T>>(const T & elt)>;

template<typename T, typename CompletionToken>
auto computeClosure(std::set<T> startElts, std::set<T> & res, GetEdgesAsync<T> getEdges, CompletionToken && token)
{
    auto initiator = [&res, startElts = std::move(startElts), getEdges = std::move(getEdges)](auto handler) {
        auto executor = asio::make_strand(asio::get_associated_executor(handler));

        /* Hand-rolled dynamic async graph traversal. ASIO/Cobalt and standard
         * C++ will get channels and task groups at some point, but this will
         * have to suffice for now. */
        struct State : std::enable_shared_from_this<State>
        {
            decltype(executor) executor;
            decltype(getEdges) getEdges;
            decltype(handler) handler;
            std::set<T> & res;
            /**
             * Needed to keep the ctx.run() alive because actual work might be happening on another thread
             * (like with FileTransfer case).
             */
            asio::executor_work_guard<decltype(State::executor)> workGuard;
            std::size_t pending = 0;
            /**
             * Whether the completion handler has been called.
             */
            bool done = false;
            /**
             * Amount of coroutines currently in flight.
             */
            std::size_t inFlight = 0;
            /**
             * Maximum number of concurrent coroutines. Implements primitive rate limiting.
             */
            std::size_t maxConcurrent = 64;
            /**
             * Nodes to handle next.
             */
            std::queue<T> todo;

            State(
                decltype(executor) executor_, decltype(getEdges) getEdges, decltype(handler) handler, std::set<T> & res)
                : executor(executor_)
                , getEdges(std::move(getEdges))
                , handler(std::move(handler))
                , res(res)
                , workGuard(asio::make_work_guard(executor_))
            {
            }

            void complete(std::exception_ptr ex)
            {
                if (std::exchange(done, true))
                    return;
                workGuard.reset(); /* We are done and we can release the lock. */
                asio::post(executor, [state = this->shared_from_this(), ex] { state->handler(ex); });
            }

            void enqueue(const std::set<T> & elts)
            {
                for (const auto & elt : elts)
                    enqueue(elt);
            }

            void spawnWorker(const T & elt)
            {
                ++inFlight;
                auto state = this->shared_from_this();
                asio::post(executor, [state = this->shared_from_this(), elt = std::move(elt)] {
                    asio::co_spawn(
                        state->executor,
                        [state, elt]() -> asio::awaitable<void> {
                            try {
                                state->enqueue(co_await state->getEdges(elt));
                            } catch (...) {
                                state->complete(std::current_exception());
                            }
                            state->onWorkDone();
                        },
                        asio::detached);
                });
            }

            void onWorkDone()
            {
                --inFlight;
                --pending;

                if (!todo.empty()) {
                    auto next = std::move(todo.front());
                    todo.pop();
                    asio::post(executor, [state = this->shared_from_this(), next = std::move(next)]() mutable {
                        state->spawnWorker(std::move(next));
                    });
                } else if (pending == 0) {
                    complete(std::exception_ptr{});
                }
            }

            void enqueue(const T & elt)
            {
                if (done)
                    return;

                if (!res.insert(elt).second)
                    return;

                ++pending;

                if (inFlight < maxConcurrent) {
                    spawnWorker(elt);
                } else {
                    todo.push(elt);
                }
            }
        };

        auto state = make_ref<State>(executor, std::move(getEdges), std::move(handler), res);
        if (startElts.empty()) {
            /* No work to do. */
            state->complete(std::exception_ptr{});
            return;
        }

        asio::post(executor, [state, startElts = std::move(startElts)] { state->enqueue(startElts); });
    };

    return asio::async_initiate<CompletionToken, void(std::exception_ptr)>(std::move(initiator), token);
}

template<typename T>
void computeClosure(std::set<T> startElts, std::set<T> & res, GetEdgesAsync<T> getEdges)
{
    asio::io_context ctx;
    std::exception_ptr ex = nullptr;
    computeClosure(
        std::move(startElts),
        res,
        std::move(getEdges),
        asio::bind_executor(ctx.get_executor(), [&](std::exception_ptr ex2) { ex = ex2; }));
    ctx.run();
    if (ex)
        std::rethrow_exception(ex);
}

} // namespace nix
