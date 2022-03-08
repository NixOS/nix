#include "gc-store.hh"

namespace nix {

GcStore & GcStore::require(Store & store)
{
    auto * gcStore = dynamic_cast<GcStore *>(&store);
    if (!gcStore)
        throw UsageError("Garbage collection not supported by store '%s'", store.getUri());
    return *gcStore;
}

}
