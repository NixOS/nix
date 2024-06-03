#pragma once

#include <functional>
#include <queue>
#include <future>

#include "sync.hh"
#include "logging.hh"
#include "environment-variables.hh"
#include "util.hh"

#include <gc.h>

namespace nix {

struct Executor
{
    using work_t = std::function<void()>;

    //std::future<void> enqueue(work_t work);

    struct State
    {
        std::queue<std::pair<std::promise<void>, work_t>> queue;
        std::vector<std::thread> threads;
        bool quit = false;
    };

    Sync<State> state_;

    std::condition_variable wakeup;

    Executor()
    {
        auto nrCores = string2Int<size_t>(getEnv("NR_CORES").value_or("1")).value_or(1);
        printError("USING %d THREADS", nrCores);
        auto state(state_.lock());
        for (size_t n = 0; n < nrCores; ++n)
            state->threads.push_back(std::thread([&]()
            {
                GC_stack_base sb;
                GC_get_stack_base(&sb);
                GC_register_my_thread(&sb);
                worker();
                GC_unregister_my_thread();
            }));
    }

    ~Executor()
    {
        std::vector<std::thread> threads;
        {
            auto state(state_.lock());
            state->quit = true;
            std::swap(threads, state->threads);
            printError("%d ITEMS LEFT", state->queue.size());
        }

        wakeup.notify_all();

        for (auto & thr : threads)
            thr.join();
    }

    void worker()
    {
        while (true) {
            std::pair<std::promise<void>, work_t> item;

            while (true) {
                auto state(state_.lock());
                if (state->quit) return;
                if (!state->queue.empty()) {
                    item = std::move(state->queue.front());
                    state->queue.pop();
                    break;
                }
                state.wait(wakeup);
            }

            //printError("EXEC");
            try {
                item.second();
                item.first.set_value();
            } catch (...) {
                item.first.set_exception(std::current_exception());
            }
        }
    }

    std::vector<std::future<void>> spawn(std::vector<work_t> && items)
    {
        if (items.empty()) return {};

        /*
        auto item = std::move(items.back());
        items.pop_back();
        */

        std::vector<std::future<void>> futures;

        {
            auto state(state_.lock());
            for (auto & item : items) {
                std::promise<void> promise;
                futures.push_back(promise.get_future());
                state->queue.emplace(std::move(promise), std::move(item));
            }
        }

        wakeup.notify_all(); // FIXME

        //item();

        /*
        for (auto & future : futures)
            future.get();
        */

        return futures;
    }
};

}
