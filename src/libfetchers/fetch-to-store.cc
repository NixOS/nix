#include "nix/fetchers/fetch-to-store.hh"
#include "nix/fetchers/fetchers.hh"
#include "nix/fetchers/fetch-settings.hh"
#include "nix/util/environment-variables.hh"

namespace nix {

fetchers::Cache::Key
makeSourcePathToHashCacheKey(std::string_view fingerprint, ContentAddressMethod method, const CanonPath & path)
{
    return fetchers::Cache::Key{
        "sourcePathToHash",
        {{"fingerprint", std::string(fingerprint)}, {"method", std::string{method.render()}}, {"path", path.abs()}}};
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
    return fetchToStore2(settings, store, path, mode, name, method, filter, repair).first;
}

std::pair<StorePath, Hash> fetchToStore2(
    const fetchers::Settings & settings,
    Store & store,
    const SourcePath & path,
    FetchMode mode,
    std::string_view name,
    ContentAddressMethod method,
    PathFilter * filter,
    RepairFlag repair)
{
    std::optional<fetchers::Cache::Key> cacheKey;

    auto [subpath, fingerprint] = filter ? std::pair<CanonPath, std::optional<std::string>>{path.path, std::nullopt}
                                         : path.accessor->getFingerprint(path.path);

    if (fingerprint) {
        cacheKey = makeSourcePathToHashCacheKey(*fingerprint, method, subpath);
        if (auto res = settings.getCache()->lookup(*cacheKey)) {
            auto hash = Hash::parseSRI(fetchers::getStrAttr(*res, "hash"));
            auto storePath =
                store.makeFixedOutputPathFromCA(name, ContentAddressWithReferences::fromParts(method, hash, {}));

            /* Add a temproot before the call to isValidPath to prevent accidental GC in case the
               input is cached. Note that this must be done before to avoid races. */
            if (mode != FetchMode::DryRun)
                store.addTempRoot(storePath);

            if (mode == FetchMode::DryRun || store.isValidPath(storePath)) {
                debug(
                    "source path '%s' cache hit in '%s' (hash '%s')",
                    path,
                    store.printStorePath(storePath),
                    hash.to_string(HashFormat::SRI, true));
                return {storePath, hash};
            }
            debug("source path '%s' not in store", path);
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

    auto [storePath, hash] =
        mode == FetchMode::DryRun
            ? [&]() {
                  auto [storePath, hash] = store.computeStorePath(
                      store.config.settings, name, path, method, HashAlgorithm::SHA256, {}, filter2);
                  debug(
                      "hashed '%s' to '%s' (hash '%s')",
                      path,
                      store.printStorePath(storePath),
                      hash.to_string(HashFormat::SRI, true));
                  return std::make_pair(storePath, hash);
              }()
            : [&]() {
                  // FIXME: ideally addToStore() would return the hash
                  // right away (like computeStorePath()).
                  auto storePath = store.addToStore(name, path, method, HashAlgorithm::SHA256, {}, filter2, repair);
                  auto info = store.queryPathInfo(storePath);
                  assert(info->references.empty());
                  auto hash = method == ContentAddressMethod::Raw::NixArchive ? info->narHash : ({
                      if (!info->ca || info->ca->method != method)
                          throw Error("path '%s' lacks a CA field", store.printStorePath(storePath));
                      info->ca->hash;
                  });
                  debug(
                      "copied '%s' to '%s' (hash '%s')",
                      path,
                      store.printStorePath(storePath),
                      hash.to_string(HashFormat::SRI, true));
                  return std::make_pair(storePath, hash);
              }();

    if (cacheKey)
        settings.getCache()->upsert(*cacheKey, {{"hash", hash.to_string(HashFormat::SRI, true)}});

    return {storePath, hash};
}

} // namespace nix
