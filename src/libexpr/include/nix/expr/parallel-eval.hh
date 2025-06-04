#pragma once

#include <functional>
#include <queue>
#include <future>
#include <random>

#include "nix/util/sync.hh"
#include "nix/util/logging.hh"
#include "nix/util/environment-variables.hh"
#include "nix/util/util.hh"
#include "nix/util/signals.hh"

#if NIX_USE_BOEHMGC
#  include <gc.h>
#endif

namespace nix {

struct Executor
{
    using work_t = std::function<void()>;

    struct Item
    {
        std::promise<void> promise;
        work_t work;
    };

    struct State
    {
        std::multimap<uint64_t, Item> queue;
        std::vector<std::thread> threads;
        bool quit = false;
    };

    Sync<State> state_;

    std::condition_variable wakeup;

    Executor(const EvalSettings & evalSettings)
    {
        debug("executor using %d threads", evalSettings.evalCores);
        auto state(state_.lock());
        for (size_t n = 0; n < evalSettings.evalCores; ++n)
            state->threads.push_back(std::thread([&]() {
#if NIX_USE_BOEHMGC
                GC_stack_base sb;
                GC_get_stack_base(&sb);
                GC_register_my_thread(&sb);
#endif
                worker();
#if NIX_USE_BOEHMGC
                GC_unregister_my_thread();
#endif
            }));
    }

    ~Executor()
    {
        std::vector<std::thread> threads;
        {
            auto state(state_.lock());
            state->quit = true;
            std::swap(threads, state->threads);
            debug("executor shutting down with %d items left", state->queue.size());
        }

        wakeup.notify_all();

        for (auto & thr : threads)
            thr.join();
    }

    void worker()
    {
        while (true) {
            Item item;

            while (true) {
                auto state(state_.lock());
                if (state->quit)
                    return;
                if (!state->queue.empty()) {
                    item = std::move(state->queue.begin()->second);
                    state->queue.erase(state->queue.begin());
                    break;
                }
                state.wait(wakeup);
            }

            try {
                item.work();
                item.promise.set_value();
            } catch (...) {
                item.promise.set_exception(std::current_exception());
            }
        }
    }

    std::vector<std::future<void>> spawn(std::vector<std::pair<work_t, uint8_t>> && items)
    {
        if (items.empty())
            return {};

        std::vector<std::future<void>> futures;

        {
            auto state(state_.lock());
            for (auto & item : items) {
                std::promise<void> promise;
                futures.push_back(promise.get_future());
                thread_local std::random_device rd;
                thread_local std::uniform_int_distribution<uint64_t> dist(0, 1ULL << 48);
                auto key = (uint64_t(item.second) << 48) | dist(rd);
                state->queue.emplace(key, Item{.promise = std::move(promise), .work = std::move(item.first)});
            }
        }

        wakeup.notify_all(); // FIXME

        return futures;
    }
};

struct FutureVector
{
    Executor & executor;

    struct State
    {
        std::vector<std::future<void>> futures;
    };

    Sync<State> state_;

    void spawn(std::vector<std::pair<Executor::work_t, uint8_t>> && work)
    {
        auto futures = executor.spawn(std::move(work));
        auto state(state_.lock());
        for (auto & future : futures)
            state->futures.push_back(std::move(future));
    }

    void spawn(uint8_t prioPrefix, Executor::work_t && work)
    {
        spawn({{std::move(work), prioPrefix}});
    }

    void finishAll()
    {
        while (true) {
            std::vector<std::future<void>> futures;
            {
                auto state(state_.lock());
                std::swap(futures, state->futures);
            }
            debug("got %d futures", futures.size());
            if (futures.empty())
                break;
            std::exception_ptr ex;
            for (auto & future : futures)
                try {
                    future.get();
                } catch (...) {
                    if (ex) {
                        if (!getInterrupted())
                            ignoreExceptionExceptInterrupt();
                    } else
                        ex = std::current_exception();
                }
            if (ex)
                std::rethrow_exception(ex);
        }
    }
};

}
