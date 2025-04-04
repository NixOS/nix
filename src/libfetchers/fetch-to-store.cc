#include "nix/fetchers/fetch-to-store.hh"
#include "nix/fetchers/fetchers.hh"

namespace nix {

fetchers::Cache::Key makeFetchToStoreCacheKey(
    const std::string &name,
    const std::string &fingerprint,
    ContentAddressMethod method,
    const std::string &path)
{
    return fetchers::Cache::Key{"fetchToStore", {
        {"name", name},
        {"fingerprint", fingerprint},
        {"method", std::string{method.render()}},
        {"path", path}
    }};

}

StorePath fetchToStore(
    Store & store,
    const SourcePath & path,
    FetchMode mode,
    std::string_view name,
    ContentAddressMethod method,
    PathFilter * filter,
    RepairFlag repair)
{
    // FIXME: add an optimisation for the case where the accessor is
    // a `PosixSourceAccessor` pointing to a store path.

    std::optional<fetchers::Cache::Key> cacheKey;

    if (!filter && path.accessor->fingerprint) {
        cacheKey = makeFetchToStoreCacheKey(std::string{name}, *path.accessor->fingerprint, method, path.path.abs());
        if (auto res = fetchers::getCache()->lookupStorePath(*cacheKey, store)) {
            debug("store path cache hit for '%s'", path);
            return res->storePath;
        }
    } else
        debug("source path '%s' is uncacheable", path);

    Activity act(*logger, lvlChatty, actUnknown,
        fmt(mode == FetchMode::DryRun ? "hashing '%s'" : "copying '%s' to the store", path));

    auto filter2 = filter ? *filter : defaultPathFilter;

    auto storePath =
        mode == FetchMode::DryRun
        ? store.computeStorePath(
            name, path, method, HashAlgorithm::SHA256, {}, filter2).first
        : store.addToStore(
            name, path, method, HashAlgorithm::SHA256, {}, filter2, repair);

    debug(mode == FetchMode::DryRun ? "hashed '%s'" : "copied '%s' to '%s'", path, store.printStorePath(storePath));

    if (cacheKey && mode == FetchMode::Copy)
        fetchers::getCache()->upsert(*cacheKey, store, {}, storePath);

    return storePath;
}

}
