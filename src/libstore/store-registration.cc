#include "nix/store/store-registration.hh"
#include "nix/store/store-open.hh"
#include "nix/store/local-store.hh"
#include "nix/store/uds-remote-store.hh"
#include "nix/store/globals.hh"
#include "nix/util/environment-variables.hh"
#include "nix/util/thread-pool.hh"

#include <filesystem>

namespace nix {

ref<Store> openStore()
{
    return openStore(StoreReference{settings.storeUri.get()});
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
                /* In the `auto` case, we are deciding between
                   `UdsRemoteStore::Config` and `LocalStore::Config`. Both of
                   them inherit from `LocalFSStore::Config`, so we are making a
                   valid assumption if we try to parse the params with that in
                   order to figure out exactly where sort of store config we're
                   supposed to resolve. */

                /* Concrete subclass of `LocalFSStoreConfig` for testing, since
                   `LocalFSStoreConfig` is abstract (`openStore()` is pure
                   virtual). */
                struct TempLocalFSStoreConfig : LocalFSStore::Config
                {
                    TempLocalFSStoreConfig(const Params & params)
                        : StoreConfig(params, FilePathType::Native)
                        , LocalFSStoreConfig(params)
                    {
                    }

                    ref<Store> openStore() const override
                    {
                        unreachable();
                    }
                } localFSStoreConfig{params};
                if (
#ifdef _WIN32
                    _waccess
#else
                    access
#endif
                    (localFSStoreConfig.stateDir.get().c_str(), R_OK | W_OK)
                    == 0)
                    return make_ref<LocalStore::Config>(params);
                else if (pathExists(getDaemonSocketPath(localFSStoreConfig)))
                    return make_ref<UDSRemoteStore::Config>(params);
#ifdef __linux__
                else if (
                    !pathExists(localFSStoreConfig.stateDir.get()) && params.empty() && !isRootUser()
                    && !getEnvOs("NIX_STORE_DIR").has_value() && !getEnvOs("NIX_STATE_DIR").has_value()) {
                    /* If /nix doesn't exist, there is no daemon socket, and
                       we're not root, then automatically set up a chroot
                       store in ~/.local/share/nix/root. */
                    auto chrootStore = getDataDir() / "root";
                    auto logLevel = lvlDebug;
                    if (!pathExists(chrootStore)) {
                        try {
                            createDirs(chrootStore);
                        } catch (SystemError & e) {
                            return make_ref<LocalStore::Config>(params);
                        }
                        logLevel = lvlWarn;
                    }
                    printMsg(
                        logLevel,
                        "%s does not exist, so Nix will use %s as a chroot store",
                        PathFmt(localFSStoreConfig.stateDir.get()),
                        PathFmt(chrootStore));
                    return make_ref<LocalStore::Config>(std::filesystem::path(chrootStore), params);
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

std::list<ref<Store>> getDefaultSubstituters()
{
    static auto stores([]() {
        std::set<StoreReference> done;
        std::vector<StoreReference> refs;
        for (const auto & ref : settings.getWorkerSettings().substituters.get())
            if (done.insert(ref).second)
                refs.push_back(ref);

        /* Open them all at once. Opening an HTTP cache means fetching
           its `nix-cache-info`, and an unreachable one takes a connect
           timeout (or a few) to fail, so opening one after the other
           makes the wait the sum of those rather than the longest. One
           slot per reference keeps the order deterministic for the
           sort below.

           TODO: a thread per store is overkill for what is purely
           waiting on network requests. The file transfer already has
           its own thread; all that is missing is an async `openStore`.
           With that, this becomes: queue them all, then block on them
           all. */
        std::vector<std::shared_ptr<Store>> opened(refs.size());
        ThreadPool pool;
        for (size_t i = 0; i < refs.size(); ++i)
            pool.enqueue([&, i]() {
                try {
                    opened[i] = openStore(StoreReference{refs[i]}).get_ptr();
                } catch (Error & e) {
                    logWarning(e.info());
                }
            });
        pool.process();

        std::list<ref<Store>> stores;
        for (auto & store : opened)
            if (store)
                stores.push_back(ref<Store>(store));

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
