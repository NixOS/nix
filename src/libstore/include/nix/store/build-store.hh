#pragma once
///@file

#include "nix/store/store-api.hh"
#include "nix/store/build.hh"

namespace nix {

/**
 * A store that can build derivations.
 *
 * Not all stores can build; in particular, we don't consider that even
 * `LocalStore` can build. Rather we consider that local building is done by Nix
 * *in* some local store.
 *
 * (This is because local building requires other global resources not-confined
 * to a specific local store, like build users, etc. It is also because if we
 * were to do building with multiple local stores for whatever reason, we should
 * reuse the same `Worker` with all of them. See #5025 for details.)
 *
 * The only stores we consider can build are remote stores which are happy to
 * encapsulate over the *entire* process: storage, scheduling, and actual local
 * building. Only those stores bypass `Worker` to do all these things, and they
 * need the top-level methods to have complete ownership over the
 * implementation.
 */
class BuildStore : public virtual Store
{
public:

    using Config = StoreConfig;

    using Store::Store;

    /**
     * Get a `Builder` for this store.
     *
     * @param evalStore If provided and different from this store,
     * derivation files will be copied from the eval store to this
     * store before building.
     */
    virtual ref<Builder> getBuilder(std::shared_ptr<Store> evalStore = nullptr) = 0;
};

} // namespace nix
