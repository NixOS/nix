#include "local-store.hh"

namespace nix {

/**
 * Configuration for `LocalOverlayStore`.
 */
struct LocalOverlayStoreConfig : virtual LocalStoreConfig
{
    // FIXME why doesn't this work?
    // using LocalStoreConfig::LocalStoreConfig;

    LocalOverlayStoreConfig(const StringMap & params)
        : StoreConfig(params)
        , LocalFSStoreConfig(params)
        , LocalStoreConfig(params)
    { }

    const Setting<std::string> lowerStoreUri{(StoreConfig*) this, "", "lower-store",
        R"(
          [Store URL](@docroot@/command-ref/new-cli/nix3-help-stores.md#store-url-format)
          for the lower store. The default is `auto` (i.e. use the Nix daemon or `/nix/store` directly).

          Must be a store with a store dir on the file system.
        )"};

    const std::string name() override { return "Experimental Local Overlay Store"; }

    std::string doc() override
    {
        return
          ""
          // FIXME write docs
          //#include "local-overlay-store.md"
          ;
    }
};

/**
 * Variation of local store using overlayfs for the store dir.
 */
class LocalOverlayStore : public virtual LocalOverlayStoreConfig, public virtual LocalStore
{
    /**
     * The store beneath us.
     *
     * Our store dir should be an overlay fs where the lower layer
     * is that store's store dir, and the upper layer is some
     * scratch storage just for us.
     */
    ref<LocalFSStore> lowerStore;

public:
    LocalOverlayStore(const Params & params);

    LocalOverlayStore(std::string scheme, std::string path, const Params & params)
        : LocalOverlayStore(params)
    {
        throw UnimplementedError("LocalOverlayStore");
    }

    static std::set<std::string> uriSchemes()
    { return {}; }

    std::string getUri() override
    {
        return "local-overlay";
    }

private:
    // Overridden methods…
};

}
