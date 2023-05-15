#include "local-overlay-store.hh"
#include "callback.hh"
#include "url.hh"
#include <regex>

namespace nix {

Path LocalOverlayStoreConfig::toUpperPath(const StorePath & path) {
    return upperLayer + "/" + path.to_string();
}

LocalOverlayStore::LocalOverlayStore(const Params & params)
    : StoreConfig(params)
    , LocalFSStoreConfig(params)
    , LocalStoreConfig(params)
    , LocalOverlayStoreConfig(params)
    , Store(params)
    , LocalFSStore(params)
    , LocalStore(params)
    , lowerStore(openStore(percentDecode(lowerStoreUri.get())).dynamic_pointer_cast<LocalFSStore>())
{
    if (checkMount.get()) {
        std::smatch match;
        std::string mountInfo;
        auto mounts = readFile("/proc/self/mounts");
        auto regex = std::regex(R"((^|\n)overlay )" + realStoreDir.get() + R"( .*(\n|$))");

        // Mount points can be stacked, so there might be multiple matching entries.
        // Loop until the last match, which will be the current state of the mount point.
        while (std::regex_search(mounts, match, regex)) {
            mountInfo = match.str();
            mounts = match.suffix();
        }

        auto checkOption = [&](std::string option, std::string value) {
            return std::regex_search(mountInfo, std::regex("\\b" + option + "=" + value + "( |,)"));
        };

        auto expectedLowerDir = lowerStore->realStoreDir.get();
        if (!checkOption("lowerdir", expectedLowerDir) || !checkOption("upperdir", upperLayer)) {
            debug("expected lowerdir: %s", expectedLowerDir);
            debug("expected upperdir: %s", upperLayer);
            debug("actual mount: %s", mountInfo);
            throw Error("overlay filesystem '%s' mounted incorrectly",
                realStoreDir.get());
        }
    }
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

    LocalStore::queryPathInfoUncached(path,
        {[this, path, callbackPtr](std::future<std::shared_ptr<const ValidPathInfo>> fut) {
            try {
                auto info = fut.get();
                if (info)
                    return (*callbackPtr)(std::move(info));
            } catch (...) {
                return callbackPtr->rethrow();
            }
            // If we don't have it, check lower store
            lowerStore->queryPathInfo(path,
                {[path, callbackPtr](std::future<ref<const ValidPathInfo>> fut) {
                    try {
                        (*callbackPtr)(fut.get().get_ptr());
                    } catch (...) {
                        return callbackPtr->rethrow();
                    }
                }});
        }});
}


void LocalOverlayStore::queryRealisationUncached(const DrvOutput & drvOutput,
    Callback<std::shared_ptr<const Realisation>> callback) noexcept 
{
    auto callbackPtr = std::make_shared<decltype(callback)>(std::move(callback));

    LocalStore::queryRealisationUncached(drvOutput,
        {[this, drvOutput, callbackPtr](std::future<std::shared_ptr<const Realisation>> fut) {
            try {
                auto info = fut.get();
                if (info)
                    return (*callbackPtr)(std::move(info));
            } catch (...) {
                return callbackPtr->rethrow();
            }
            // If we don't have it, check lower store
            lowerStore->queryRealisation(drvOutput,
                {[callbackPtr](std::future<std::shared_ptr<const Realisation>> fut) {
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
    if (res) return res;
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


StorePathSet LocalOverlayStore::queryValidDerivers(const StorePath & path)
{
    auto res = LocalStore::queryValidDerivers(path);
    for (auto p : lowerStore->queryValidDerivers(path))
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


void LocalOverlayStore::addToStore(const ValidPathInfo & info, Source & source,
    RepairFlag repair, CheckSigsFlag checkSigs)
{
    LocalStore::addToStore(info, source, repair, checkSigs);
    if (lowerStore->isValidPath(info.path)) {
        // dedup stores
        deletePath(toUpperPath(info.path));
    }
}


StorePath LocalOverlayStore::addToStoreFromDump(Source & dump, std::string_view name,
    FileIngestionMethod method, HashType hashAlgo, RepairFlag repair, const StorePathSet & references)
{
    auto path = LocalStore::addToStoreFromDump(dump, name, method, hashAlgo, repair, references);
    if (lowerStore->isValidPath(path)) {
        // dedup stores
        deletePath(toUpperPath(path));
    }
    return path;
}


StorePath LocalOverlayStore::addTextToStore(
    std::string_view name,
    std::string_view s,
    const StorePathSet & references,
    RepairFlag repair)
{
    auto path = LocalStore::addTextToStore(name, s, references, repair);
    if (lowerStore->isValidPath(path)) {
        // dedup stores
        deletePath(toUpperPath(path));
    }
    return path;
}


static RegisterStoreImplementation<LocalOverlayStore, LocalOverlayStoreConfig> regLocalOverlayStore;

}
