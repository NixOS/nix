#include "store-path-accessor.hh"
#include "store-api.hh"

namespace nix {

ref<SourceAccessor> makeStorePathAccessor(ref<Store> store, const StorePath & storePath)
{
    return projectSubdirSourceAccessor(store->getFSAccessor(), storePath.to_string());
}

}
