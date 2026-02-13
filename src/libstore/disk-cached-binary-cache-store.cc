#include "nix/store/disk-cached-binary-cache-store.hh"
#include "nix/util/logging.hh"
#include "nix/util/callback.hh"

namespace nix {

DiskCachedBinaryCacheStore::DiskCachedBinaryCacheStore(ref<BinaryCacheStore> inner, ref<NarInfoDiskCache> diskCache)
    : Store{inner->config}
    , BinaryCacheStore{inner->config}
    , inner{inner}
    , diskCache{diskCache}
{
}

std::string DiskCachedBinaryCacheStore::cacheUri()
{
    return inner->config.getReference().render(/*withParams=*/false);
}

void DiskCachedBinaryCacheStore::init()
{
    auto cacheKey = cacheUri();

    // Check if we have cached info about this binary cache
    if (auto cacheInfo = diskCache->upToDateCacheExists(cacheKey)) {
        inner->config.wantMassQuery.setDefault(cacheInfo->wantMassQuery);
        inner->config.priority.setDefault(cacheInfo->priority);
    } else {
        // Initialize the inner store to fetch cache info
        inner->init();
        diskCache->createCache(cacheKey, storeDir, inner->config.wantMassQuery, inner->config.priority);
    }
}

bool DiskCachedBinaryCacheStore::isValidPathUncached(const StorePath & storePath)
{
    auto res = diskCache->lookupNarInfo(cacheUri(), std::string(storePath.hashPart()));
    if (res.first != NarInfoDiskCache::oUnknown) {
        stats.narInfoReadAverted++;
        return res.first == NarInfoDiskCache::oValid;
    }

    // Call the full isValidPath on inner, which will use inner's caching
    bool valid = inner->isValidPath(storePath);

    if (!valid)
        diskCache->upsertNarInfo(cacheUri(), std::string(storePath.hashPart()), 0);

    return valid;
}

void DiskCachedBinaryCacheStore::queryPathInfoUncached(
    const StorePath & storePath, Callback<std::shared_ptr<const ValidPathInfo>> callback) noexcept
{
    auto hashPart = std::string(storePath.hashPart());

    try {
        auto res = diskCache->lookupNarInfo(cacheUri(), hashPart);
        if (res.first != NarInfoDiskCache::oUnknown) {
            stats.narInfoReadAverted++;
            if (res.first == NarInfoDiskCache::oValid) {
                return callback(res.second);
            } else {
                return callback(nullptr);
            }
        }
    } catch (...) {
        return callback.rethrow();
    }

    auto callbackPtr = std::make_shared<decltype(callback)>(std::move(callback));

    // Call the full queryPathInfo on inner
    inner->queryPathInfo(storePath, {[this, hashPart, callbackPtr](std::future<ref<const ValidPathInfo>> fut) {
                             try {
                                 auto info = fut.get();
                                 diskCache->upsertNarInfo(cacheUri(), hashPart, info.get_ptr());
                                 (*callbackPtr)(info.get_ptr());
                             } catch (InvalidPath &) {
                                 diskCache->upsertNarInfo(cacheUri(), hashPart, 0);
                                 (*callbackPtr)(nullptr);
                             } catch (...) {
                                 callbackPtr->rethrow();
                             }
                         }});
}

void DiskCachedBinaryCacheStore::queryRealisationUncached(
    const DrvOutput & id, Callback<std::shared_ptr<const UnkeyedRealisation>> callback) noexcept
{
    try {
        auto [cacheOutcome, maybeCachedRealisation] = diskCache->lookupRealisation(cacheUri(), id);
        switch (cacheOutcome) {
        case NarInfoDiskCache::oValid:
            debug("Returning a cached realisation for %s", id.to_string());
            callback(maybeCachedRealisation);
            return;
        case NarInfoDiskCache::oInvalid:
            debug("Returning a cached missing realisation for %s", id.to_string());
            callback(nullptr);
            return;
        case NarInfoDiskCache::oUnknown:
            break;
        }
    } catch (...) {
        return callback.rethrow();
    }

    auto callbackPtr = std::make_shared<decltype(callback)>(std::move(callback));

    // Call the full queryRealisation on inner
    inner->queryRealisation(id, {[this, id, callbackPtr](std::future<std::shared_ptr<const UnkeyedRealisation>> fut) {
                                try {
                                    auto info = fut.get();

                                    if (info)
                                        diskCache->upsertRealisation(cacheUri(), {*info, id});
                                    else
                                        diskCache->upsertAbsentRealisation(cacheUri(), id);

                                    (*callbackPtr)(std::shared_ptr<const UnkeyedRealisation>(info));

                                } catch (...) {
                                    callbackPtr->rethrow();
                                }
                            }});
}

void DiskCachedBinaryCacheStore::writeNarInfo(ref<NarInfo> narInfo)
{
    inner->writeNarInfo(narInfo);

    diskCache->upsertNarInfo(cacheUri(), std::string(narInfo->path.hashPart()), std::shared_ptr<NarInfo>(narInfo));
}

void DiskCachedBinaryCacheStore::registerDrvOutput(const Realisation & info)
{
    diskCache->upsertRealisation(cacheUri(), info);
    inner->registerDrvOutput(info);
}

// Backend storage methods - delegate to inner store

bool DiskCachedBinaryCacheStore::fileExists(const std::string & path)
{
    return inner->fileExists(path);
}

void DiskCachedBinaryCacheStore::upsertFile(
    const std::string & path, RestartableSource & source, const std::string & mimeType, uint64_t sizeHint)
{
    inner->upsertFile(path, source, mimeType, sizeHint);
}

void DiskCachedBinaryCacheStore::getFile(const std::string & path, Sink & sink)
{
    inner->getFile(path, sink);
}

void DiskCachedBinaryCacheStore::getFile(
    const std::string & path, Callback<std::optional<std::string>> callback) noexcept
{
    inner->getFile(path, std::move(callback));
}

std::optional<TrustedFlag> DiskCachedBinaryCacheStore::isTrustedClient()
{
    return inner->isTrustedClient();
}

} // namespace nix
