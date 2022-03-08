#include "log-store.hh"

namespace nix {

LogStore & LogStore::require(Store & store)
{
    auto * gcStore = dynamic_cast<LogStore *>(&store);
    if (!gcStore)
        throw UsageError("Build log storage and retrieval not supported by store '%s'", store.getUri());
    return *gcStore;
}

}
