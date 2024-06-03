#include "store-path-accessor.hh"
#include "store-api.hh"

namespace nix {

ref<SourceAccessor> makeStorePathAccessor(ref<Store> store, const StorePath & storePath)
{
    // FIXME: should use `store->getFSAccessor()`
    auto root = std::filesystem::path{store->toRealPath(storePath)};
    auto accessor = makeFSSourceAccessor(root);
    accessor->setPathDisplay(root.string());
    return accessor;
}

}
