#include "eval.hh"

namespace nix {

struct WaiterDomain
{
    std::condition_variable cv;
};

static std::array<Sync<WaiterDomain>, 128> waiterDomains;

static Sync<WaiterDomain> & getWaiterDomain(Value & v)
{
    //auto domain = std::hash<Value *>{}(&v) % waiterDomains.size();
    auto domain = (((size_t) &v) >> 5) % waiterDomains.size();
    debug("HASH %x -> %d", &v, domain);
    return waiterDomains[domain];
}

void EvalState::waitOnThunk(Value & v, bool awaited)
{
    auto domain = getWaiterDomain(v).lock();

    if (awaited) {
        /* Make sure that the value is still awaited, now that we're
           holding the domain lock. */
        auto type = v.internalType.load();

        /* If the value has been finalized in the meantime (i.e is no
           longer pending), we're done. */
        if (type != tAwaited) {
            debug("VALUE DONE RIGHT AWAY 2 %x", &v);
            assert(type != tThunk && type != tApp && type != tPending && type != tAwaited);
            return;
        }
    } else {
        /* Mark this value as being waited on. */
        auto type = tPending;
        if (!v.internalType.compare_exchange_strong(type, tAwaited)) {
            /* If the value has been finalized in the meantime (i.e is
               no longer pending), we're done. */
            if (type != tAwaited) {
                debug("VALUE DONE RIGHT AWAY %x", &v);
                assert(type != tThunk && type != tApp && type != tPending && type != tAwaited);
                return;
            }
            /* The value was already in the "waited on" state, so we're
               not the only thread waiting on it. */
            debug("ALREADY AWAITED %x", &v);
        } else
            debug("PENDING -> AWAITED %x", &v);
    }

    debug("AWAIT %x", &v);

    while (true) {
        domain.wait(domain->cv);
        debug("WAKEUP %x", &v);
        auto type = v.internalType.load();
        if (type != tAwaited) {
            if (type == tFailed)
                std::rethrow_exception(v.payload.failed->ex);
            assert(type != tThunk && type != tApp && type != tPending && type != tAwaited);
            return;
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
