#include "nix/store/store-registration.hh"
#include "nix/store/store-open.hh"
#include "nix/store/local-store.hh"
#include "nix/store/uds-remote-store.hh"
#include "nix/store/globals.hh"
#include <chrono>
#include <regex>

namespace nix {

ref<Store> openStore()
{
    return openStore(settings.storeUri.get());
}

ref<Store> openStore(const std::string & uri, const Store::Config::Params & extraParams)
{
    return openStore(StoreReference::parse(uri, extraParams));
}

ref<Store> openStore(StoreReference && storeURI)
{
    auto store = resolveStoreConfig(std::move(storeURI))->openStore();
    store->init();
    return store;
}

ref<StoreConfig> resolveStoreConfig(StoreReference && storeURI)
{
    auto & params = storeURI.params;

    auto storeConfig = std::visit(
        overloaded{
            [&](const StoreReference::Auto &) -> ref<StoreConfig> {
                auto stateDir = getOr(params, "state", settings.nixStateDir);
                if (access(stateDir.c_str(), R_OK | W_OK) == 0)
                    return make_ref<LocalStore::Config>(params);
                else if (pathExists(settings.nixDaemonSocketFile))
                    return make_ref<UDSRemoteStore::Config>(params);
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
                            return make_ref<LocalStore::Config>(params);
                        }
                        warn("'%s' does not exist, so Nix will use '%s' as a chroot store", stateDir, chrootStore);
                    } else
                        debug("'%s' does not exist, so Nix will use '%s' as a chroot store", stateDir, chrootStore);
                    return make_ref<LocalStore::Config>("local", chrootStore, params);
                }
#endif
                else
                    return make_ref<LocalStore::Config>(params);
            },
            [&](const StoreReference::Specified & g) {
                for (const auto & [storeName, implem] : Implementations::registered())
                    if (implem.uriSchemes.count(g.scheme))
                        return implem.parseConfig(g.scheme, g.authority, params);

                throw Error("don't know how to open Nix store with scheme '%s'", g.scheme);
            },
        },
        storeURI.variant);

    experimentalFeatureSettings.require(storeConfig->experimentalFeature());
    storeConfig->warnUnknownSettings();

    return storeConfig;
}

/**
 * Check if an error during store initialization is recoverable (network/timeout)
 * vs permanent (configuration, authentication, etc.)
 */
static bool isRecoverableStoreError(const Error & e) {
    std::string msg = e.what();
    std::string lowerMsg = msg;
    std::transform(lowerMsg.begin(), lowerMsg.end(), lowerMsg.begin(), ::tolower);
    
    // Network and timeout errors that might be temporary
    return lowerMsg.find("timeout") != std::string::npos ||
           lowerMsg.find("timed out") != std::string::npos ||
           lowerMsg.find("connection timeout") != std::string::npos ||
           lowerMsg.find("connection refused") != std::string::npos ||
           lowerMsg.find("network unreachable") != std::string::npos ||
           lowerMsg.find("could not resolve") != std::string::npos ||
           lowerMsg.find("temporary failure") != std::string::npos ||
           lowerMsg.find("service unavailable") != std::string::npos ||
           lowerMsg.find("curl error") != std::string::npos ||
           msg.find("curl: (6)") != std::string::npos ||  // Couldn't resolve host  
           msg.find("curl: (7)") != std::string::npos ||  // Couldn't connect
           msg.find("curl: (28)") != std::string::npos || // Timeout was reached
           msg.find("curl: (56)") != std::string::npos;   // Connection reset
}

std::list<ref<Store>> getDefaultSubstituters()
{
    static auto stores([]() {
        std::list<ref<Store>> stores;

        StringSet done;

        auto addStore = [&](const std::string & uri) {
            if (!done.insert(uri).second)
                return;
            try {
                auto store = resolveStoreConfig(StoreReference::parse(uri))->openStore();
                // Try to initialize the store, but don't drop it if init fails
                try {
                    store->init();
                } catch (Error & e) {
                    if (isRecoverableStoreError(e)) {
                        // Mark store as having failed init, but keep it in the list
                        warn("substituter '%s' failed during initialization (%s), will retry during substitution", 
                             uri, e.what());
                    } else {
                        // For non-recoverable errors, still warn but keep the store
                        warn("substituter '%s' failed during initialization (%s), may not be usable", 
                             uri, e.what());
                    }
                }
                stores.push_back(store);
            } catch (Error & e) {
                // Only drop stores that fail to even create (config errors, etc.)
                if (!isRecoverableStoreError(e)) {
                    logWarning(e.info());
                } else {
                    warn("substituter '%s' could not be created (%s), skipping", uri, e.what());
                }
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
