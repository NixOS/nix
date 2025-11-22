#include "nix/util/json-utils.hh"
#include "nix/util/archive.hh"
#include "nix/util/posix-source-accessor.hh"
#include "nix/store/store-api.hh"
#include "nix/store/local-fs-store.hh"
#include "nix/store/globals.hh"
#include "nix/util/compression.hh"
#include "nix/store/derivations.hh"
#include "nix/store/config-parse-impl.hh"

namespace nix {

constexpr static const LocalFSStoreConfigT<config::SettingInfo> localFSStoreConfigDescriptions = {
    .rootDir{
        .name = "root",
        .description = "Directory prefixed to all other paths.",
    },
    .stateDir{
        .name = "state",
        .description = "Directory where Nix stores state.",
    },
    .logDir{
        .name = "log",
        .description = "directory where Nix stores log files.",
    },
    .realStoreDir{
        .name = "real",
        .description = "Physical path of the Nix store.",
    },
};

#define LOCAL_FS_STORE_CONFIG_FIELDS(X) X(rootDir), X(stateDir), X(logDir), X(realStoreDir),

MAKE_PARSE(LocalFSStoreConfig, localFSStoreConfig, LOCAL_FS_STORE_CONFIG_FIELDS)

/**
 * @param rootDir Fallback if not in `params`
 */
static LocalFSStoreConfigT<config::PlainValue>
localFSStoreConfigDefaults(const Path & storeDir, const std::optional<Path> & rootDir)
{
    return {
        .rootDir = std::nullopt,
        .stateDir = rootDir ? *rootDir + "/nix/var/nix" : settings.nixStateDir,
        .logDir = rootDir ? *rootDir + "/nix/var/log/nix" : settings.nixLogDir,
        .realStoreDir = rootDir ? *rootDir + "/nix/store" : storeDir,
    };
}

static LocalFSStoreConfigT<config::PlainValue>
localFSStoreConfigApplyParse(const Path & storeDir, LocalFSStoreConfigT<config::OptionalValue> parsed)
{
    auto defaults = localFSStoreConfigDefaults(storeDir, parsed.rootDir.value_or(std::nullopt));
    return {LOCAL_FS_STORE_CONFIG_FIELDS(APPLY_ROW_SEP_DEFAULTS)};
}

config::SettingDescriptionMap LocalFSStoreConfig::descriptions()
{
    constexpr auto & descriptions = localFSStoreConfigDescriptions;
    auto defaults = localFSStoreConfigDefaults(settings.nixStore, std::nullopt);
    return {LOCAL_FS_STORE_CONFIG_FIELDS(DESCRIBE_ROW_SEP_DEFAULTS)};
}

LocalFSStore::Config::LocalFSStoreConfig(const Store::Config & storeConfig, const StoreConfig::Params & params)
    : LocalFSStoreConfigT<config::PlainValue>{localFSStoreConfigApplyParse(
          storeConfig.storeDir, localFSStoreConfigParse(params))}
    , storeConfig{storeConfig}
{
}

static LocalFSStoreConfigT<config::OptionalValue>
applyAuthority(LocalFSStoreConfigT<config::OptionalValue> parsed, PathView rootDir)
{
    if (!rootDir.empty())
        parsed.rootDir = std::optional{Path{rootDir}};
    return parsed;
}

LocalFSStore::Config::LocalFSStoreConfig(
    const Store::Config & storeConfig, PathView rootDir, const StoreConfig::Params & params)
    : LocalFSStoreConfigT<config::PlainValue>{localFSStoreConfigApplyParse(
          storeConfig.storeDir, applyAuthority(localFSStoreConfigParse(params), rootDir))}
    , storeConfig{storeConfig}
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
        : PosixSourceAccessor(std::filesystem::path{store->config.realStoreDir})
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

std::shared_ptr<SourceAccessor> LocalFSStore::getFSAccessor(const StorePath & path, bool requireValidPath)
{
    auto absPath = std::filesystem::path{config.realStoreDir} / path.to_string();
    if (requireValidPath) {
        /* Only return non-null if the store object is a fully-valid
           member of the store. */
        if (!isValidPath(path))
            return nullptr;
    } else {
        /* Return non-null as long as the some file system data exists,
           even if the store object is not fully registered. */
        if (!pathExists(absPath))
            return nullptr;
    }
    return std::make_shared<PosixSourceAccessor>(std::move(absPath));
}

const std::string LocalFSStore::drvsLogDir = "drvs";

std::optional<std::string> LocalFSStore::getBuildLogExact(const StorePath & path)
{
    auto baseName = path.to_string();

    for (int j = 0; j < 2; j++) {

        Path logPath = j == 0 ? fmt("%s/%s/%s/%s", config.logDir, drvsLogDir, baseName.substr(0, 2), baseName.substr(2))
                              : fmt("%s/%s/%s", config.logDir, drvsLogDir, baseName);
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
