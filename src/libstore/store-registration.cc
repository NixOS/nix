#include "store-registration.hh"
#include "store-open.hh"
#include "local-store.hh"
#include "uds-remote-store.hh"

namespace nix {

ref<Store> openStore(const std::string & uri, const Store::Params & extraParams)
{
    return openStore(StoreReference::parse(uri, extraParams));
}

ref<Store> openStore(StoreReference && storeURI)
{
    auto & params = storeURI.params;

    auto store = std::visit(
        overloaded{
            [&](const StoreReference::Auto &) -> std::shared_ptr<Store> {
                auto stateDir = getOr(params, "state", settings.nixStateDir);
                if (access(stateDir.c_str(), R_OK | W_OK) == 0)
                    return std::make_shared<LocalStore>(params);
                else if (pathExists(settings.nixDaemonSocketFile))
                    return std::make_shared<UDSRemoteStore>(params);
#if __linux__
                else if (
                    !pathExists(stateDir) && params.empty() && !isRootUser() && !getEnv("NIX_STORE_DIR").has_value()
                    && !getEnv("NIX_STATE_DIR").has_value()) {
                    /* If /nix doesn't exist, there is no daemon socket, and
                       we're not root, then automatically set up a chroot
                       store in ~/.local/share/nix/root. */
                    auto chrootStore = getDataDir() + "/nix/root";
                    if (!pathExists(chrootStore)) {
                        try {
                            createDirs(chrootStore);
                        } catch (SystemError & e) {
                            return std::make_shared<LocalStore>(params);
                        }
                        warn("'%s' does not exist, so Nix will use '%s' as a chroot store", stateDir, chrootStore);
                    } else
                        debug("'%s' does not exist, so Nix will use '%s' as a chroot store", stateDir, chrootStore);
                    return std::make_shared<LocalStore>("local", chrootStore, params);
                }
#endif
                else
                    return std::make_shared<LocalStore>(params);
            },
            [&](const StoreReference::Specified & g) {
                for (auto implem : *Implementations::registered)
                    if (implem.uriSchemes.count(g.scheme))
                        return implem.create(g.scheme, g.authority, params);

                throw Error("don't know how to open Nix store with scheme '%s'", g.scheme);
            },
        },
        storeURI.variant);

    experimentalFeatureSettings.require(store->experimentalFeature());
    store->warnUnknownSettings();
    store->init();

    return ref<Store>{store};
}

std::vector<StoreFactory> * Implementations::registered = 0;

std::list<ref<Store>> getDefaultSubstituters()
{
    static auto stores([]() {
        std::list<ref<Store>> stores;

        StringSet done;

        auto addStore = [&](const std::string & uri) {
            if (!done.insert(uri).second)
                return;
            try {
                stores.push_back(openStore(uri));
            } catch (Error & e) {
                logWarning(e.info());
            }
        };

        for (auto uri : settings.substituters.get())
            addStore(uri);

        stores.sort([](ref<Store> & a, ref<Store> & b) { return a->priority < b->priority; });

        return stores;
    }());

    return stores;
}

}
