#include "local-overlay-store.hh"

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


static RegisterStoreImplementation<LocalOverlayStore, LocalOverlayStoreConfig> regLocalOverlayStore;

}
