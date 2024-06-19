#include "eval.hh"

namespace nix {

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

std::atomic<uint64_t> nrThunksAwaited, nrThunksAwaitedSlow, usWaiting, currentlyWaiting, maxWaiting;

InternalType EvalState::waitOnThunk(Value & v, bool awaited)
{
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
        printError("SPURIOUS %s", &v);
    }
}

void Value::notifyWaiters()
{
    debug("NOTIFY %x", this);

    auto domain = getWaiterDomain(*this).lock();

    domain->cv.notify_all(); // FIXME
}

}
