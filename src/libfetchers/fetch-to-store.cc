#include "fetch-to-store.hh"
#include "fetchers.hh"
#include "cache.hh"

namespace nix {

StorePath fetchToStore(
    ref<Store> store,
    const SourcePath & path,
    std::string_view name,
    FileIngestionMethod method,
    PathFilter * filter,
    RepairFlag repair)
{
    // FIXME: add an optimisation for the case where the accessor is
    // an FSInputAccessor pointing to a store path.

    std::optional<fetchers::Attrs> cacheKey;

    if (!filter && path.accessor->fingerprint) {
        cacheKey = fetchers::Attrs{
            {"_what", "fetchToStore"},
            {"store", store->storeDir},
            {"name", std::string(name)},
            {"fingerprint", *path.accessor->fingerprint},
            {"method", (uint8_t) method},
            {"path", path.path.abs()}
        };
        if (auto res = fetchers::getCache()->lookup(store, *cacheKey)) {
            debug("store path cache hit for '%s'", path);
            return res->second;
        }
    } else
        debug("source path '%s' is uncacheable", path);

    Activity act(*logger, lvlChatty, actUnknown, fmt("copying '%s' to the store", path));

    auto source = sinkToSource([&](Sink & sink) {
        if (method == FileIngestionMethod::Recursive)
            path.accessor->dumpPath(path.path, sink, filter ? *filter : defaultPathFilter);
        else
            path.accessor->readFile(path.path, sink);
    });

    auto storePath =
        settings.readOnlyMode
        ? store->computeStorePathFromDump(*source, name, method, HashAlgorithm::SHA256).first
        : store->addToStoreFromDump(*source, name, method, HashAlgorithm::SHA256, repair);

    if (cacheKey)
        fetchers::getCache()->add(store, *cacheKey, {}, storePath, true);

    return storePath;
}


}
