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

    Executor(const EvalSettings & evalSettings);

    ~Executor();

    void worker();

    std::vector<std::future<void>> spawn(std::vector<std::pair<work_t, uint8_t>> && items);
};

struct FutureVector
{
    Executor & executor;

    struct State
    {
        std::vector<std::future<void>> futures;
    };

    Sync<State> state_;

    // FIXME: add a destructor that cancels/waits for all futures.

    void spawn(std::vector<std::pair<Executor::work_t, uint8_t>> && work);

    void spawn(uint8_t prioPrefix, Executor::work_t && work)
    {
        spawn({{std::move(work), prioPrefix}});
    }

    void finishAll();
};

}
