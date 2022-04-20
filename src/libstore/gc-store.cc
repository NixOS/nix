#include "gc-store.hh"

namespace nix {

GcStore & requireGcStore(Store & store)
{
    auto * gcStore = dynamic_cast<GcStore *>(&store);
    if (!gcStore)
        throw UsageError("Garbage collection not supported by this store");
    return *gcStore;
}

}
