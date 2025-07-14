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

struct WaiterDomain
{
    std::condition_variable cv;
};

static std::array<Sync<WaiterDomain>, 128> waiterDomains;

static Sync<WaiterDomain> & getWaiterDomain(detail::ValueBase & v)
{
    auto domain = (((size_t) &v) >> 5) % waiterDomains.size();
    debug("HASH %x -> %d", &v, domain);
    return waiterDomains[domain];
}

template<>
ValueStorage<sizeof(void *)>::PackedPointer ValueStorage<sizeof(void *)>::waitOnThunk(EvalState & state, bool awaited)
{
    state.nrThunksAwaited++;

    auto domain = getWaiterDomain(*this).lock();

    if (awaited) {
        /* Make sure that the value is still awaited, now that we're
           holding the domain lock. */
        auto p0_ = p0.load(std::memory_order_acquire);
        auto pd = static_cast<PrimaryDiscriminator>(p0_ & discriminatorMask);

        /* If the value has been finalized in the meantime (i.e. is no
           longer pending), we're done. */
        if (pd != pdAwaited) {
            debug("VALUE DONE RIGHT AWAY 2 %x", this);
            assert(pd != pdThunk && pd != pdPending);
            return p0_;
        }
    } else {
        /* Mark this value as being waited on. */
        PackedPointer p0_ = pdPending;
        if (!p0.compare_exchange_strong(p0_, pdAwaited, std::memory_order_relaxed, std::memory_order_acquire)) {
            /* If the value has been finalized in the meantime (i.e. is
               no longer pending), we're done. */
            auto pd = static_cast<PrimaryDiscriminator>(p0_ & discriminatorMask);
            if (pd != pdAwaited) {
                debug("VALUE DONE RIGHT AWAY %x", this);
                assert(pd != pdThunk && pd != pdPending);
                return p0_;
            }
            /* The value was already in the "waited on" state, so we're
               not the only thread waiting on it. */
            debug("ALREADY AWAITED %x", this);
        } else
            debug("PENDING -> AWAITED %x", this);
    }

    /* Wait for another thread to finish this value. */
    debug("AWAIT %x", this);

    if (state.settings.evalCores <= 1)
        state.error<InfiniteRecursionError>("infinite recursion encountered")
            .atPos(((Value &) *this).determinePos(noPos))
            .debugThrow();

    state.nrThunksAwaitedSlow++;
    state.currentlyWaiting++;
    state.maxWaiting = std::max(
        state.maxWaiting.load(std::memory_order_acquire), state.currentlyWaiting.load(std::memory_order_acquire));

    auto now1 = std::chrono::steady_clock::now();

    while (true) {
        domain.wait(domain->cv);
        debug("WAKEUP %x", this);
        auto p0_ = p0.load(std::memory_order_acquire);
        auto pd = static_cast<PrimaryDiscriminator>(p0_ & discriminatorMask);
        if (pd != pdAwaited) {
            assert(pd != pdThunk && pd != pdPending);
            auto now2 = std::chrono::steady_clock::now();
            state.usWaiting += std::chrono::duration_cast<std::chrono::microseconds>(now2 - now1).count();
            state.currentlyWaiting--;
            return p0_;
        }
        state.nrSpuriousWakeups++;
    }
}

template<>
void ValueStorage<sizeof(void *)>::notifyWaiters()
{
    debug("NOTIFY %x", this);

    auto domain = getWaiterDomain(*this).lock();

    domain->cv.notify_all();
}

}
