#include "store-registration.hh"
#include "store-open.hh"
#include "local-store.hh"
#include "uds-remote-store.hh"
#include "json-utils.hh"

namespace nix {

ref<Store> openStore(const std::string & uri, const StoreReference::Params & extraParams)
{
    return openStore(StoreReference::parse(uri, extraParams));
}

ref<Store> openStore(StoreReference && storeURI)
{
    auto store = resolveStoreConfig(std::move(storeURI))->openStore();

#if 0 // FIXME
    store->warnUnknownSettings();
    store->init();
#endif

    return store;
}

ref<StoreConfig> resolveStoreConfig(StoreReference && storeURI)
{
    auto & params = storeURI.params;

    auto storeConfig = std::visit(
        overloaded{
            [&](const StoreReference::Auto &) -> ref<StoreConfig> {
                auto stateDir = getString(getOr(params, "state", settings.nixStateDir));
                if (access(stateDir.c_str(), R_OK | W_OK) == 0)
                    return make_ref<LocalStore::Config>(params);
                else if (pathExists(settings.nixDaemonSocketFile))
                    return make_ref<UDSRemoteStore::Config>(params);
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
                for (auto & [name, implem] : *Implementations::registered)
                    if (implem.uriSchemes.count(g.scheme))
                        return implem.parseConfig(g.scheme, g.authority, params);

                throw Error("don't know how to open Nix store with scheme '%s'", g.scheme);
            },
        },
        storeURI.variant);

    experimentalFeatureSettings.require(storeConfig->experimentalFeature());

    return storeConfig;
}

Implementations::V * Implementations::registered = 0;

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

        stores.sort([](ref<Store> & a, ref<Store> & b) {
            return a->resolvedSubstConfig.priority < b->resolvedSubstConfig.priority;
        });

        return stores;
    }());

    return stores;
}

}

namespace nlohmann {

using namespace nix::config;

ref<const StoreConfig> adl_serializer<ref<const StoreConfig>>::from_json(const json & json)
{
    StoreReference ref;
    switch (json.type()) {

    case json::value_t::string: {
        ref = StoreReference::parse(json.get_ref<const std::string &>());
        break;
    }

    case json::value_t::object: {
        auto & obj = json.get_ref<const json::object_t &>();
        ref = StoreReference{
            .variant =
                StoreReference::Specified{
                    .scheme = getString(valueAt(obj, "scheme")),
                    .authority = getString(valueAt(obj, "authority")),
                },
            .params = obj,
        };
        break;
    }

    case json::value_t::null:
    case json::value_t::number_unsigned:
    case json::value_t::number_integer:
    case json::value_t::number_float:
    case json::value_t::boolean:
    case json::value_t::array:
    case json::value_t::binary:
    case json::value_t::discarded:
    default:
        throw UsageError(
            "Invalid JSON for Store configuration: is type '%s' but must be string or object", json.type_name());
    };

    return resolveStoreConfig(std::move(ref));
}

void adl_serializer<ref<const StoreConfig>>::to_json(json & obj, ref<const StoreConfig> s)
{
    // TODO, for tests maybe
    assert(false);
}

}
