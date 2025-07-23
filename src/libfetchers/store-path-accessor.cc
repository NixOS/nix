#include "nix/fetchers/store-path-accessor.hh"
#include "nix/store/store-api.hh"

namespace nix {

ref<SourceAccessor> makeStorePathAccessor(ref<Store> store, const StorePath & storePath)
{
    return projectSubdirSourceAccessor(store->getFSAccessor(), storePath.to_string());
}

} // namespace nix
