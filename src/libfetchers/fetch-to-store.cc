#include "fetch-to-store.hh"
#include "fetchers.hh"
#include "cache.hh"

namespace nix {

StorePath fetchToStore(
    Store & store,
    const SourcePath & path,
    std::string_view name,
    ContentAddressMethod method,
    PathFilter * filter,
    RepairFlag repair)
{
    // FIXME: add an optimisation for the case where the accessor is
    // an FSInputAccessor pointing to a store path.

    std::optional<fetchers::Attrs> cacheKey;

    if (!filter && path.accessor->fingerprint) {
        cacheKey = fetchers::Attrs{
            {"_what", "fetchToStore"},
            {"store", store.storeDir},
            {"name", std::string(name)},
            {"fingerprint", *path.accessor->fingerprint},
            {
                "method",
                std::visit(overloaded {
                    [](const TextIngestionMethod &) {
                        return "text";
                    },
                    [](const FileIngestionMethod & fim) {
                        switch (fim) {
                        case FileIngestionMethod::Flat: return "flat";
                        case FileIngestionMethod::Recursive: return "nar";
                        default: assert(false);
                        }
                    },
                }, method.raw),
            },
            {"path", path.path.abs()}
        };
        if (auto res = fetchers::getCache()->lookup(store, *cacheKey)) {
            debug("store path cache hit for '%s'", path);
            return res->second;
        }
    } else
        debug("source path '%s' is uncacheable", path);

    Activity act(*logger, lvlChatty, actUnknown, fmt("copying '%s' to the store", path));

    auto filter2 = filter ? *filter : defaultPathFilter;

    auto storePath =
        settings.readOnlyMode
        ? store.computeStorePath(
            name, *path.accessor, path.path, method, HashAlgorithm::SHA256, {}, filter2).first
        : store.addToStore(
            name, *path.accessor, path.path, method, HashAlgorithm::SHA256, {}, filter2, repair);

    if (cacheKey)
        fetchers::getCache()->add(store, *cacheKey, {}, storePath, true);

    return storePath;
}


}
