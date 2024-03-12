#include "eval.hh"

namespace nix {

void EvalState::waitOnPendingThunk(Value & v)
{
    /* Mark this value as being waited on. */
    auto type = tPending;
    if (!v.internalType.compare_exchange_strong(type, tAwaited)) {
        /* If the value has been finalized in the meantime (i.e is no
           longer pending), we're done. */
        if (type != tAwaited) {
            printError("VALUE DONE RIGHT AWAY");
            assert(type != tThunk && type != tApp && type != tPending && type != tAwaited);
            return;
        }
        /* The value was already in the "waited on" state, so we're
           not the only thread waiting on it. */
    }

    printError("AWAIT %x", &v);
}

void Value::notifyWaiters()
{
    printError("NOTIFY %x", this);
}

}
