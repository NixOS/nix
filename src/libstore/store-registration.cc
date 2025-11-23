#include "nix/store/store-registration.hh"
#include "nix/store/store-open.hh"
#include "nix/store/local-store.hh"
#include "nix/store/uds-remote-store.hh"
#include "nix/store/globals.hh"

namespace nix {

ref<Store> openStore(Settings & settings)
{
    return openStore(settings, settings.storeUri.get());
}

ref<Store> openStore(Settings & settings, const std::string & uri, const Store::Config::Params & extraParams)
{
    return openStore(settings, StoreReference::parse(uri, extraParams));
}

ref<Store> openStore(Settings & settings, StoreReference && storeURI)
{
    auto store = resolveStoreConfig(settings, std::move(storeURI))->openStore();
    store->init();
    return store;
}

ref<StoreConfig> resolveStoreConfig(Settings & settings, StoreReference && storeURI)
{
    auto & params = storeURI.params;

    auto storeConfig = std::visit(
        overloaded{
            [&](const StoreReference::Auto &) -> ref<StoreConfig> {
                auto stateDir = getOr(params, "state", settings.nixStateDir);
                if (access(stateDir.c_str(), R_OK | W_OK) == 0)
                    return make_ref<LocalStore::Config>(settings, params);
                else if (pathExists(settings.nixDaemonSocketFile))
                    return make_ref<UDSRemoteStore::Config>(settings, params);
#ifdef __linux__
                else if (
                    !pathExists(stateDir) && params.empty() && !isRootUser() && !getEnv("NIX_STORE_DIR").has_value()
                    && !getEnv("NIX_STATE_DIR").has_value()) {
                    /* If /nix doesn't exist, there is no daemon socket, and
                       we're not root, then automatically set up a chroot
                       store in ~/.local/share/nix/root. */
                    auto chrootStore = getDataDir() + "/root";
                    if (!pathExists(chrootStore)) {
                        try {
                            createDirs(chrootStore);
                        } catch (SystemError & e) {
                            return make_ref<LocalStore::Config>(settings, params);
                        }
                        warn("'%s' does not exist, so Nix will use '%s' as a chroot store", stateDir, chrootStore);
                    } else
                        debug("'%s' does not exist, so Nix will use '%s' as a chroot store", stateDir, chrootStore);
                    return make_ref<LocalStore::Config>(settings, "local", chrootStore, params);
                }
#endif
                else
                    return make_ref<LocalStore::Config>(settings, params);
            },
            [&](const StoreReference::Specified & g) {
                for (const auto & [storeName, implem] : Implementations::registered())
                    if (implem.uriSchemes.count(g.scheme))
                        return implem.parseConfig(settings, g.scheme, g.authority, params);

                throw Error("don't know how to open Nix store with scheme '%s'", g.scheme);
            },
        },
        storeURI.variant);

    experimentalFeatureSettings.require(storeConfig->experimentalFeature());
    storeConfig->warnUnknownSettings();

    return storeConfig;
}

std::list<ref<Store>> getDefaultSubstituters(Settings & settings)
{
    static auto stores([&]() {
        std::list<ref<Store>> stores;

        StringSet done;

        auto addStore = [&](const std::string & uri) {
            if (!done.insert(uri).second)
                return;
            try {
                stores.push_back(openStore(settings, uri));
            } catch (Error & e) {
                logWarning(e.info());
            }
        };

        for (const auto & uri : settings.substituters.get())
            addStore(uri);

        stores.sort([](ref<Store> & a, ref<Store> & b) { return a->config.priority < b->config.priority; });

        return stores;
    }());

    return stores;
}

Implementations::Map & Implementations::registered()
{
    static Map registered;
    return registered;
}

} // namespace nix
