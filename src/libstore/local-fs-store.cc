#include "archive.hh"
#include "posix-source-accessor.hh"
#include "store-api.hh"
#include "local-fs-store.hh"
#include "globals.hh"
#include "compression.hh"
#include "derivations.hh"
#include "config-parse-impl.hh"

namespace nix {

LocalFSStore::Config::Descriptions::Descriptions()
    : Store::Config::Descriptions{Store::Config::descriptions}
    , LocalFSStoreConfigT<config::SettingInfo>{
        .rootDir = {
            .name = "root",
            .description = "Directory prefixed to all other paths.",
        },
        .stateDir = {
            .name = "state",
            .description = "Directory where Nix will store state.",
        },
        .logDir = {
            .name = "log",
            .description = "directory where Nix will store log files.",
        },
        .realStoreDir{
            .name = "real",
            .description = "Physical path of the Nix store.",
        },
    }
{}

const LocalFSStore::Config::Descriptions LocalFSStore::Config::descriptions{};

LocalFSStoreConfigT<config::JustValue> LocalFSStore::Config::defaults(
    const Store::Config & storeConfig,
    const std::optional<Path> rootDir)
{
    return {
        .rootDir = {std::nullopt},
        .stateDir = {rootDir ? *rootDir + "/nix/var/nix" : settings.nixStateDir},
        .logDir = {rootDir ? *rootDir + "/nix/var/log/nix" : settings.nixLogDir},
        .realStoreDir = {rootDir ? *rootDir + "/nix/store" : storeConfig.storeDir},
    };
}

/**
 * @param rootDir Fallback if not in `params`
 */
auto localFSStoreConfig(
    const Store::Config & storeConfig,
    const std::optional<Path> _rootDir,
    const StoreReference::Params & params)
{
    const auto & descriptions = LocalFSStore::Config::descriptions;

    auto rootDir = descriptions.rootDir.parseConfig(params)
        .value_or(config::JustValue{.value = std::move(_rootDir)});

    auto defaults = LocalFSStore::Config::defaults(storeConfig, rootDir.value);

    return LocalFSStoreConfigT<config::JustValue>{
        CONFIG_ROW(rootDir),
        CONFIG_ROW(stateDir),
        CONFIG_ROW(logDir),
        CONFIG_ROW(realStoreDir),
    };
}

LocalFSStore::Config::LocalFSStoreConfig(const StoreReference::Params & params)
    : StoreConfig{params}
    , LocalFSStoreConfigT<config::JustValue>{localFSStoreConfig(*this, std::nullopt, params)}
{
}

LocalFSStore::Config::LocalFSStoreConfig(PathView rootDir, const StoreReference::Params & params)
    : StoreConfig(params)
    , LocalFSStoreConfigT<config::JustValue>{localFSStoreConfig(
        *this,
        // Default `?root` from `rootDir` if non set
        !rootDir.empty() ? (std::optional<Path>{rootDir}) : std::nullopt,
        params)}
{
}

LocalFSStore::LocalFSStore(const Config & config)
    : LocalFSStore::Config{config}
    , Store{static_cast<const Store::Config &>(*this)}
{
}

struct LocalStoreAccessor : PosixSourceAccessor
{
    ref<LocalFSStore> store;
    bool requireValidPath;

    LocalStoreAccessor(ref<LocalFSStore> store, bool requireValidPath)
        : store(store)
        , requireValidPath(requireValidPath)
    { }

    CanonPath toRealPath(const CanonPath & path)
    {
        auto [storePath, rest] = store->toStorePath(path.abs());
        if (requireValidPath && !store->isValidPath(storePath))
            throw InvalidPath("path '%1%' is not a valid store path", store->printStorePath(storePath));
        return CanonPath(store->getRealStoreDir()) / storePath.to_string() / CanonPath(rest);
    }

    std::optional<Stat> maybeLstat(const CanonPath & path) override
    {
        /* Handle the case where `path` is (a parent of) the store. */
        if (isDirOrInDir(store->storeDir, path.abs()))
            return Stat{ .type = tDirectory };

        return PosixSourceAccessor::maybeLstat(toRealPath(path));
    }

    DirEntries readDirectory(const CanonPath & path) override
    {
        return PosixSourceAccessor::readDirectory(toRealPath(path));
    }

    void readFile(
        const CanonPath & path,
        Sink & sink,
        std::function<void(uint64_t)> sizeCallback) override
    {
        return PosixSourceAccessor::readFile(toRealPath(path), sink, sizeCallback);
    }

    std::string readLink(const CanonPath & path) override
    {
        return PosixSourceAccessor::readLink(toRealPath(path));
    }
};

ref<SourceAccessor> LocalFSStore::getFSAccessor(bool requireValidPath)
{
    return make_ref<LocalStoreAccessor>(ref<LocalFSStore>(
            std::dynamic_pointer_cast<LocalFSStore>(shared_from_this())),
        requireValidPath);
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
            j == 0
            ? fmt("%s/%s/%s/%s", logDir.get(), drvsLogDir, baseName.substr(0, 2), baseName.substr(2))
            : fmt("%s/%s/%s", logDir.get(), drvsLogDir, baseName);
        Path logBz2Path = logPath + ".bz2";

        if (pathExists(logPath))
            return readFile(logPath);

        else if (pathExists(logBz2Path)) {
            try {
                return decompress("bzip2", readFile(logBz2Path));
            } catch (Error &) { }
        }

    }

    return std::nullopt;
}

}
