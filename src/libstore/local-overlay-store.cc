#include <regex>

#include "nix/store/local-overlay-store.hh"
#include "nix/util/callback.hh"
#include "nix/store/realisation.hh"
#include "nix/util/processes.hh"
#include "nix/util/url.hh"
#include "nix/store/store-api.hh"
#include "nix/store/store-registration.hh"
#include "nix/store/config-parse-impl.hh"

namespace nix {

static LocalOverlayStoreConfigT<config::SettingInfoWithDefault> localOverlayStoreConfigDescriptions = {
    .lowerStoreConfig{
        {
            .name = "lower-store",
            .description = R"(
              [Store URL](@docroot@/command-ref/new-cli/nix3-help-stores.md#store-url-format)
              for the lower store. The default is `auto` (i.e. use the Nix daemon or `/nix/store` directly).

              Must be a store with a store dir on the file system.
              Must be used as OverlayFS lower layer for this store's store dir.
            )",
            // It's not actually machine-specific, but we don't yet have a
            // `to_json` for `StoreConfig`.
            .documentDefault = false,
        },
        {
            .makeDefault = []() -> ref<const StoreConfig> {
                return make_ref<LocalStore::Config>(StoreConfig::Params{});
            },
        },
    },
    .upperLayer{
        {
            .name = "upper-layer",
            .description = R"(
              Directory containing the OverlayFS upper layer for this store's store dir.
            )",
        },
        {
            .makeDefault = [] { return Path{}; },
        },
    },
    .checkMount{
        {
            .name = "check-mount",
            .description = R"(
              Check that the overlay filesystem is correctly mounted.

              Nix does not manage the overlayfs mount point itself, but the correct
              functioning of the overlay store does depend on this mount point being set up
              correctly. Rather than just assume this is the case, check that the lowerdir
              and upperdir options are what we expect them to be. This check is on by
              default, but can be disabled if needed.
            )",
        },
        {
            .makeDefault = [] { return true; },
        },
    },
    .remountHook{
        {
            .name = "remount-hook",
            .description = R"(
              Script or other executable to run when overlay filesystem needs remounting.

              This is occasionally necessary when deleting a store path that exists in both upper and lower layers.
              In such a situation, bypassing OverlayFS and deleting the path in the upper layer directly
              is the only way to perform the deletion without creating a "whiteout".
              However this causes the OverlayFS kernel data structures to get out-of-sync,
              and can lead to 'stale file handle' errors; remounting solves the problem.

              The store directory is passed as an argument to the invoked executable.
            )",
        },
        {
            .makeDefault = [] { return Path{}; },
        },
    },
};

#define LOCAL_OVERLAY_STORE_CONFIG_FIELDS(X) X(lowerStoreConfig), X(upperLayer), X(checkMount), X(remountHook),

MAKE_PARSE(LocalOverlayStoreConfig, localOverlayStoreConfig, LOCAL_OVERLAY_STORE_CONFIG_FIELDS)

MAKE_APPLY_PARSE(LocalOverlayStoreConfig, localOverlayStoreConfig, LOCAL_OVERLAY_STORE_CONFIG_FIELDS)

config::SettingDescriptionMap LocalOverlayStoreConfig::descriptions()
{
    config::SettingDescriptionMap ret;
    ret.merge(StoreConfig::descriptions());
    ret.merge(LocalFSStoreConfig::descriptions());
    ret.merge(LocalStoreConfig::descriptions());
    {
        constexpr auto & descriptions = localOverlayStoreConfigDescriptions;
        ret.merge(decltype(ret){LOCAL_OVERLAY_STORE_CONFIG_FIELDS(DESCRIBE_ROW)});
    }
    return ret;
}

LocalOverlayStore::Config::LocalOverlayStoreConfig(
    std::string_view scheme, std::string_view authority, const StoreConfig::Params & params)
    : LocalStore::Config(scheme, authority, params)
    , LocalOverlayStoreConfigT<config::PlainValue>{localOverlayStoreConfigApplyParse(params)}
{
}

std::string LocalOverlayStoreConfig::doc()
{
    return
#include "local-overlay-store.md"
        ;
}

ref<Store> LocalOverlayStoreConfig::openStore() const
{
    return make_ref<LocalOverlayStore>(
        ref{std::dynamic_pointer_cast<const LocalOverlayStoreConfig>(shared_from_this())});
}

StoreReference LocalOverlayStoreConfig::getReference() const
{
    return {
        .variant =
            StoreReference::Specified{
                .scheme = *uriSchemes().begin(),
            },
    };
}

Path LocalOverlayStoreConfig::toUpperPath(const StorePath & path) const
{
    return upperLayer + "/" + path.to_string();
}

LocalOverlayStore::LocalOverlayStore(ref<const Config> config)
    : Store{*config}
    , LocalFSStore{*config}
    , LocalStore{static_cast<ref<const LocalStore::Config>>(config)}
    , config{config}
    , lowerStore(config->lowerStoreConfig->openStore().dynamic_pointer_cast<LocalFSStore>())
{
    if (config->checkMount) {
        std::smatch match;
        std::string mountInfo;
        auto mounts = readFile(std::filesystem::path{"/proc/self/mounts"});
        auto regex = std::regex(R"((^|\n)overlay )" + config->realStoreDir + R"( .*(\n|$))");

        // Mount points can be stacked, so there might be multiple matching entries.
        // Loop until the last match, which will be the current state of the mount point.
        while (std::regex_search(mounts, match, regex)) {
            mountInfo = match.str();
            mounts = match.suffix();
        }

        auto checkOption = [&](std::string option, std::string value) {
            return std::regex_search(mountInfo, std::regex("\\b" + option + "=" + value + "( |,)"));
        };

        auto expectedLowerDir = lowerStore->config.realStoreDir;
        if (!checkOption("lowerdir", expectedLowerDir) || !checkOption("upperdir", config->upperLayer)) {
            debug("expected lowerdir: %s", expectedLowerDir);
            debug("expected upperdir: %s", config->upperLayer);
            debug("actual mount: %s", mountInfo);
            throw Error("overlay filesystem '%s' mounted incorrectly", config->realStoreDir);
        }
    }
}

void LocalOverlayStore::registerDrvOutput(const Realisation & info)
{
    // First do queryRealisation on lower layer to populate DB
    auto res = lowerStore->queryRealisation(info.id);
    if (res)
        LocalStore::registerDrvOutput({*res, info.id});

    LocalStore::registerDrvOutput(info);
}

void LocalOverlayStore::queryPathInfoUncached(
    const StorePath & path, Callback<std::shared_ptr<const ValidPathInfo>> callback) noexcept
{
    auto callbackPtr = std::make_shared<decltype(callback)>(std::move(callback));

    LocalStore::queryPathInfoUncached(
        path, {[this, path, callbackPtr](std::future<std::shared_ptr<const ValidPathInfo>> fut) {
            try {
                auto info = fut.get();
                if (info)
                    return (*callbackPtr)(std::move(info));
            } catch (...) {
                return callbackPtr->rethrow();
            }
            // If we don't have it, check lower store
            lowerStore->queryPathInfo(path, {[path, callbackPtr](std::future<ref<const ValidPathInfo>> fut) {
                                          try {
                                              (*callbackPtr)(fut.get().get_ptr());
                                          } catch (...) {
                                              return callbackPtr->rethrow();
                                          }
                                      }});
        }});
}

void LocalOverlayStore::queryRealisationUncached(
    const DrvOutput & drvOutput, Callback<std::shared_ptr<const UnkeyedRealisation>> callback) noexcept
{
    auto callbackPtr = std::make_shared<decltype(callback)>(std::move(callback));

    LocalStore::queryRealisationUncached(
        drvOutput, {[this, drvOutput, callbackPtr](std::future<std::shared_ptr<const UnkeyedRealisation>> fut) {
            try {
                auto info = fut.get();
                if (info)
                    return (*callbackPtr)(std::move(info));
            } catch (...) {
                return callbackPtr->rethrow();
            }
            // If we don't have it, check lower store
            lowerStore->queryRealisation(
                drvOutput, {[callbackPtr](std::future<std::shared_ptr<const UnkeyedRealisation>> fut) {
                    try {
                        (*callbackPtr)(fut.get());
                    } catch (...) {
                        return callbackPtr->rethrow();
                    }
                }});
        }});
}

bool LocalOverlayStore::isValidPathUncached(const StorePath & path)
{
    auto res = LocalStore::isValidPathUncached(path);
    if (res)
        return res;
    res = lowerStore->isValidPath(path);
    if (res) {
        // Get path info from lower store so upper DB genuinely has it.
        auto p = lowerStore->queryPathInfo(path);
        // recur on references, syncing entire closure.
        for (auto & r : p->references)
            if (r != path)
                isValidPath(r);
        LocalStore::registerValidPath(*p);
    }
    return res;
}

void LocalOverlayStore::queryReferrers(const StorePath & path, StorePathSet & referrers)
{
    LocalStore::queryReferrers(path, referrers);
    lowerStore->queryReferrers(path, referrers);
}

void LocalOverlayStore::queryGCReferrers(const StorePath & path, StorePathSet & referrers)
{
    LocalStore::queryReferrers(path, referrers);
}

StorePathSet LocalOverlayStore::queryValidDerivers(const StorePath & path)
{
    auto res = LocalStore::queryValidDerivers(path);
    for (const auto & p : lowerStore->queryValidDerivers(path))
        res.insert(p);
    return res;
}

std::optional<StorePath> LocalOverlayStore::queryPathFromHashPart(const std::string & hashPart)
{
    auto res = LocalStore::queryPathFromHashPart(hashPart);
    if (res)
        return res;
    else
        return lowerStore->queryPathFromHashPart(hashPart);
}

void LocalOverlayStore::registerValidPaths(const ValidPathInfos & infos)
{
    // First, get any from lower store so we merge
    {
        StorePathSet notInUpper;
        for (auto & [p, _] : infos)
            if (!LocalStore::isValidPathUncached(p)) // avoid divergence
                notInUpper.insert(p);
        auto pathsInLower = lowerStore->queryValidPaths(notInUpper);
        ValidPathInfos inLower;
        for (auto & p : pathsInLower)
            inLower.insert_or_assign(p, *lowerStore->queryPathInfo(p));
        LocalStore::registerValidPaths(inLower);
    }
    // Then do original request
    LocalStore::registerValidPaths(infos);
}

void LocalOverlayStore::collectGarbage(const GCOptions & options, GCResults & results)
{
    LocalStore::collectGarbage(options, results);

    remountIfNecessary();
}

void LocalOverlayStore::deleteStorePath(const Path & path, uint64_t & bytesFreed)
{
    auto mergedDir = config->realStoreDir + "/";
    if (path.substr(0, mergedDir.length()) != mergedDir) {
        warn("local-overlay: unexpected gc path '%s' ", path);
        return;
    }

    StorePath storePath = {path.substr(mergedDir.length())};
    auto upperPath = config->toUpperPath(storePath);

    if (pathExists(upperPath)) {
        debug("upper exists: %s", path);
        if (lowerStore->isValidPath(storePath)) {
            debug("lower exists: %s", storePath.to_string());
            // Path also exists in lower store.
            // We must delete via upper layer to avoid creating a whiteout.
            deletePath(upperPath, bytesFreed);
            _remountRequired = true;
        } else {
            // Path does not exist in lower store.
            // So we can delete via overlayfs and not need to remount.
            LocalStore::deleteStorePath(path, bytesFreed);
        }
    }
}

void LocalOverlayStore::optimiseStore()
{
    Activity act(*logger, actOptimiseStore);

    // Note for LocalOverlayStore, queryAllValidPaths only returns paths in upper layer
    auto paths = queryAllValidPaths();

    act.progress(0, paths.size());

    uint64_t done = 0;

    for (auto & path : paths) {
        if (lowerStore->isValidPath(path)) {
            uint64_t bytesFreed = 0;
            // Deduplicate store path
            deleteStorePath(Store::toRealPath(path), bytesFreed);
        }
        done++;
        act.progress(done, paths.size());
    }

    remountIfNecessary();
}

LocalStore::VerificationResult LocalOverlayStore::verifyAllValidPaths(RepairFlag repair)
{
    StorePathSet done;

    auto existsInStoreDir = [&](const StorePath & storePath) {
        return pathExists(config->realStoreDir + "/" + storePath.to_string());
    };

    bool errors = false;
    StorePathSet validPaths;

    for (auto & i : queryAllValidPaths())
        verifyPath(i, existsInStoreDir, done, validPaths, repair, errors);

    return {
        .errors = errors,
        .validPaths = validPaths,
    };
}

void LocalOverlayStore::remountIfNecessary()
{
    if (!_remountRequired)
        return;

    if (config->remountHook.empty()) {
        warn("'%s' needs remounting, set remount-hook to do this automatically", config->realStoreDir);
    } else {
        runProgram(config->remountHook, false, {config->realStoreDir});
    }

    _remountRequired = false;
}

static RegisterStoreImplementation<LocalOverlayStore::Config> regLocalOverlayStore;

} // namespace nix
