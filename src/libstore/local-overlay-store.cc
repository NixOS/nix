#include "local-overlay-store.hh"
#include "callback.hh"

namespace nix {

LocalOverlayStore::LocalOverlayStore(const Params & params)
    : StoreConfig(params)
    , LocalFSStoreConfig(params)
    , LocalStoreConfig(params)
    , LocalOverlayStoreConfig(params)
    , Store(params)
    , LocalFSStore(params)
    , LocalStore(params)
    , lowerStore(openStore(lowerStoreUri).dynamic_pointer_cast<LocalFSStore>())
{
}


void LocalOverlayStore::registerDrvOutput(const Realisation & info)
{
    // First do queryRealisation on lower layer to populate DB
    auto res = lowerStore->queryRealisation(info.id);
    if (res)
        LocalStore::registerDrvOutput(*res);

    LocalStore::registerDrvOutput(info);
}


void LocalOverlayStore::queryPathInfoUncached(const StorePath & path,
    Callback<std::shared_ptr<const ValidPathInfo>> callback) noexcept
{

    auto callbackPtr = std::make_shared<decltype(callback)>(std::move(callback));

    // If we don't have it, check lower store
    LocalStore::queryPathInfoUncached(path,
        {[this, path, callbackPtr](std::future<std::shared_ptr<const ValidPathInfo>> fut) {
            try {
                (*callbackPtr)(fut.get());
            } catch (...) {
                lowerStore->queryPathInfo(path,
                    {[path, callbackPtr](std::future<ref<const ValidPathInfo>> fut) {
                        try {
                            (*callbackPtr)(fut.get().get_ptr());
                        } catch (...) {
                            callbackPtr->rethrow();
                        }
                    }});
            }
        }});
}


static RegisterStoreImplementation<LocalOverlayStore, LocalOverlayStoreConfig> regLocalOverlayStore;

}
