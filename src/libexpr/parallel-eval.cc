#include "nix/expr/eval.hh"
#include "nix/expr/parallel-eval.hh"

namespace nix {

thread_local bool Executor::amWorkerThread{false};

Executor::Executor(const EvalSettings & evalSettings)
    : enabled(evalSettings.evalCores > 1)
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

Executor::~Executor()
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

void Executor::worker()
{
    amWorkerThread = true;

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

std::vector<std::future<void>> Executor::spawn(std::vector<std::pair<work_t, uint8_t>> && items)
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

void FutureVector::spawn(std::vector<std::pair<Executor::work_t, uint8_t>> && work)
{
    auto futures = executor.spawn(std::move(work));
    auto state(state_.lock());
    for (auto & future : futures)
        state->futures.push_back(std::move(future));
}

void FutureVector::finishAll()
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

#if 0
struct WaiterDomain
{
    std::condition_variable cv;
};

static std::array<Sync<WaiterDomain>, 128> waiterDomains;

static Sync<WaiterDomain> & getWaiterDomain(Value & v)
{
    auto domain = (((size_t) &v) >> 5) % waiterDomains.size();
    debug("HASH %x -> %d", &v, domain);
    return waiterDomains[domain];
}
#endif

InternalType EvalState::waitOnThunk(Value & v, bool awaited)
{
#if 0
    nrThunksAwaited++;

    auto domain = getWaiterDomain(v).lock();

    if (awaited) {
        /* Make sure that the value is still awaited, now that we're
           holding the domain lock. */
        auto type = v.internalType.load(std::memory_order_acquire);

        /* If the value has been finalized in the meantime (i.e. is no
           longer pending), we're done. */
        if (type != tAwaited) {
            debug("VALUE DONE RIGHT AWAY 2 %x", &v);
            assert(isFinished(type));
            return type;
        }
    } else {
        /* Mark this value as being waited on. */
        auto type = tPending;
        if (!v.internalType.compare_exchange_strong(
                type, tAwaited, std::memory_order_relaxed, std::memory_order_acquire)) {
            /* If the value has been finalized in the meantime (i.e. is
               no longer pending), we're done. */
            if (type != tAwaited) {
                debug("VALUE DONE RIGHT AWAY %x", &v);
                assert(isFinished(type));
                return type;
            }
            /* The value was already in the "waited on" state, so we're
               not the only thread waiting on it. */
            debug("ALREADY AWAITED %x", &v);
        } else
            debug("PENDING -> AWAITED %x", &v);
    }

    /* Wait for another thread to finish this value. */
    debug("AWAIT %x", &v);

    if (settings.evalCores <= 1)
        error<InfiniteRecursionError>("infinite recursion encountered").atPos(v.determinePos(noPos)).debugThrow();

    nrThunksAwaitedSlow++;
    currentlyWaiting++;
    maxWaiting = std::max(maxWaiting.load(std::memory_order_acquire), currentlyWaiting.load(std::memory_order_acquire));

    auto now1 = std::chrono::steady_clock::now();

    while (true) {
        domain.wait(domain->cv);
        debug("WAKEUP %x", &v);
        auto type = v.internalType.load(std::memory_order_acquire);
        if (type != tAwaited) {
            assert(isFinished(type));
            auto now2 = std::chrono::steady_clock::now();
            usWaiting += std::chrono::duration_cast<std::chrono::microseconds>(now2 - now1).count();
            currentlyWaiting--;
            return type;
        }
        nrSpuriousWakeups++;
    }
#endif
    assert(false);
}

#if 0
void Value::notifyWaiters()
{
    debug("NOTIFY %x", this);

    auto domain = getWaiterDomain(*this).lock();

    domain->cv.notify_all(); // FIXME
}
#endif

}
