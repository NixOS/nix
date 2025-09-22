#include "nix/fetchers/fetch-to-store.hh"
#include "nix/fetchers/fetchers.hh"
#include "nix/fetchers/fetch-settings.hh"
#include "nix/util/environment-variables.hh"

namespace nix {

fetchers::Cache::Key makeFetchToStoreCacheKey(
    const std::string & name, const std::string & fingerprint, ContentAddressMethod method, const std::string & path)
{
    return fetchers::Cache::Key{
        "fetchToStore",
        {{"name", name}, {"fingerprint", fingerprint}, {"method", std::string{method.render()}}, {"path", path}}};
}

StorePath fetchToStore(
    const fetchers::Settings & settings,
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

    auto [subpath, fingerprint] = filter ? std::pair<CanonPath, std::optional<std::string>>{path.path, std::nullopt}
                                         : path.accessor->getFingerprint(path.path);

    if (fingerprint) {
        cacheKey = makeFetchToStoreCacheKey(std::string{name}, *fingerprint, method, subpath.abs());
        if (auto res = settings.getCache()->lookupStorePath(*cacheKey, store)) {
            debug("store path cache hit for '%s'", path);
            return res->storePath;
        }
    } else {
        static auto barf = getEnv("_NIX_TEST_BARF_ON_UNCACHEABLE").value_or("") == "1";
        if (barf && !filter)
            throw Error("source path '%s' is uncacheable (filter=%d)", path, (bool) filter);
        // FIXME: could still provide in-memory caching keyed on `SourcePath`.
        debug("source path '%s' is uncacheable", path);
    }

    Activity act(
        *logger,
        lvlChatty,
        actUnknown,
        fmt(mode == FetchMode::DryRun ? "hashing '%s'" : "copying '%s' to the store", path));

    auto filter2 = filter ? *filter : defaultPathFilter;

    auto storePath = mode == FetchMode::DryRun
                         ? store.computeStorePath(name, path, method, HashAlgorithm::SHA256, {}, filter2).first
                         : store.addToStore(name, path, method, HashAlgorithm::SHA256, {}, filter2, repair);

    debug(mode == FetchMode::DryRun ? "hashed '%s'" : "copied '%s' to '%s'", path, store.printStorePath(storePath));

    if (cacheKey && mode == FetchMode::Copy)
        settings.getCache()->upsert(*cacheKey, store, {}, storePath);

    return storePath;
}

} // namespace nix
