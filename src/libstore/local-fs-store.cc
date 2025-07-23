#include "nix/util/archive.hh"
#include "nix/util/posix-source-accessor.hh"
#include "nix/store/store-api.hh"
#include "nix/store/local-fs-store.hh"
#include "nix/store/globals.hh"
#include "nix/util/compression.hh"
#include "nix/store/derivations.hh"

namespace nix {

LocalFSStoreConfig::LocalFSStoreConfig(PathView rootDir, const Params & params)
    : StoreConfig(params)
    // Default `?root` from `rootDir` if non set
    // FIXME don't duplicate description once we don't have root setting
    , rootDir{
          this,
          !rootDir.empty() && params.count("root") == 0 ? (std::optional<Path>{rootDir}) : std::nullopt,
          "root",
          "Directory prefixed to all other paths."}
{
}

LocalFSStore::LocalFSStore(const Config & config)
    : Store{static_cast<const Store::Config &>(*this)}
    , config{config}
{
}

struct LocalStoreAccessor : PosixSourceAccessor
{
    ref<LocalFSStore> store;
    bool requireValidPath;

    LocalStoreAccessor(ref<LocalFSStore> store, bool requireValidPath)
        : PosixSourceAccessor(std::filesystem::path{store->config.realStoreDir.get()})
        , store(store)
        , requireValidPath(requireValidPath)
    {
    }

    void requireStoreObject(const CanonPath & path)
    {
        auto [storePath, rest] = store->toStorePath(store->storeDir + path.abs());
        if (requireValidPath && !store->isValidPath(storePath))
            throw InvalidPath("path '%1%' is not a valid store path", store->printStorePath(storePath));
    }

    std::optional<Stat> maybeLstat(const CanonPath & path) override
    {
        /* Also allow `path` to point to the entire store, which is
           needed for resolving symlinks. */
        if (path.isRoot())
            return Stat{.type = tDirectory};

        requireStoreObject(path);
        return PosixSourceAccessor::maybeLstat(path);
    }

    DirEntries readDirectory(const CanonPath & path) override
    {
        requireStoreObject(path);
        return PosixSourceAccessor::readDirectory(path);
    }

    void readFile(const CanonPath & path, Sink & sink, std::function<void(uint64_t)> sizeCallback) override
    {
        requireStoreObject(path);
        return PosixSourceAccessor::readFile(path, sink, sizeCallback);
    }

    std::string readLink(const CanonPath & path) override
    {
        requireStoreObject(path);
        return PosixSourceAccessor::readLink(path);
    }
};

ref<SourceAccessor> LocalFSStore::getFSAccessor(bool requireValidPath)
{
    return make_ref<LocalStoreAccessor>(
        ref<LocalFSStore>(std::dynamic_pointer_cast<LocalFSStore>(shared_from_this())), requireValidPath);
}

void LocalFSStore::narFromPath(const StorePath & path, Sink & sink)
{
    if (!isValidPath(path))
        throw Error("path '%s' is not valid", printStorePath(path));
    dumpPath(getRealStoreDir() + std::string(printStorePath(path), storeDir.size()), sink);
}

const std::string LocalFSStore::drvsLogDir = "drvs";

std::optional<std::string> LocalFSStore::getBuildLogExact(const StorePath & path)
{
    auto baseName = path.to_string();

    for (int j = 0; j < 2; j++) {

        Path logPath =
            j == 0 ? fmt("%s/%s/%s/%s", config.logDir.get(), drvsLogDir, baseName.substr(0, 2), baseName.substr(2))
                   : fmt("%s/%s/%s", config.logDir.get(), drvsLogDir, baseName);
        Path logBz2Path = logPath + ".bz2";

        if (pathExists(logPath))
            return readFile(logPath);

        else if (pathExists(logBz2Path)) {
            try {
                return decompress("bzip2", readFile(logBz2Path));
            } catch (Error &) {
            }
        }
    }

    return std::nullopt;
}

} // namespace nix
