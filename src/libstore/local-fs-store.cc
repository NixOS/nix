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
};

#define LOCAL_FS_STORE_CONFIG_FIELDS(X) \
    X(rootDir), \
    X(stateDir), \
    X(logDir), \
    X(realStoreDir),

MAKE_PARSE(LocalFSStoreConfig, localFSStoreConfig, LOCAL_FS_STORE_CONFIG_FIELDS)

/**
 * @param rootDir Fallback if not in `params`
 */
static LocalFSStoreConfigT<config::PlainValue> localFSStoreConfigDefaults(
    const Path & storeDir,
    const std::optional<Path> & rootDir)
{
    return {
        .rootDir = {std::nullopt},
        .stateDir = {rootDir ? *rootDir + "/nix/var/nix" : settings.nixStateDir},
        .logDir = {rootDir ? *rootDir + "/nix/var/log/nix" : settings.nixLogDir},
        .realStoreDir = {rootDir ? *rootDir + "/nix/store" : storeDir},
    };
}

static LocalFSStoreConfigT<config::PlainValue> localFSStoreConfigApplyParse(
    const Path & storeDir,
    LocalFSStoreConfigT<config::OptValue> parsed)
{
    auto defaults = localFSStoreConfigDefaults(
        storeDir,
        parsed.rootDir.optValue.value_or(std::nullopt));
    return {LOCAL_FS_STORE_CONFIG_FIELDS(APPLY_ROW)};
}

config::SettingDescriptionMap LocalFSStoreConfig::descriptions()
{
    constexpr auto & descriptions = localFSStoreConfigDescriptions;
    auto defaults = localFSStoreConfigDefaults(settings.nixStore, std::nullopt);
    return {
        LOCAL_FS_STORE_CONFIG_FIELDS(DESCRIBE_ROW)
    };
}

LocalFSStore::Config::LocalFSStoreConfig(
    const Store::Config & storeConfig,
    const StoreReference::Params & params)
    : LocalFSStoreConfigT<config::PlainValue>{
        localFSStoreConfigApplyParse(
            storeConfig.storeDir,
            localFSStoreConfigParse(params))}
    , storeConfig{storeConfig}
{
}

static LocalFSStoreConfigT<config::OptValue> applyAuthority(
    LocalFSStoreConfigT<config::OptValue> parsed,
    PathView rootDir)
{
    if (!rootDir.empty())
        parsed.rootDir = {.optValue = {Path{rootDir}}};
    return parsed;
}

LocalFSStore::Config::LocalFSStoreConfig(
    const Store::Config & storeConfig,
    PathView rootDir,
    const StoreReference::Params & params)
    : LocalFSStoreConfigT<config::PlainValue>{
        localFSStoreConfigApplyParse(
            storeConfig.storeDir,
            applyAuthority(
                localFSStoreConfigParse(params),
                rootDir))}
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
            return Stat{ .type = tDirectory };

        requireStoreObject(path);
        return PosixSourceAccessor::maybeLstat(path);
    }

    DirEntries readDirectory(const CanonPath & path) override
    {
        requireStoreObject(path);
        return PosixSourceAccessor::readDirectory(path);
    }

    void readFile(
        const CanonPath & path,
        Sink & sink,
        std::function<void(uint64_t)> sizeCallback) override
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
            ? fmt("%s/%s/%s/%s", config.logDir.get(), drvsLogDir, baseName.substr(0, 2), baseName.substr(2))
            : fmt("%s/%s/%s", config.logDir.get(), drvsLogDir, baseName);
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
