#include "archive.hh"
#include "posix-source-accessor.hh"
#include "store-api.hh"
#include "local-fs-store.hh"
#include "globals.hh"
#include "compression.hh"
#include "derivations.hh"
#include "config-parse-impl.hh"

namespace nix {

static const LocalFSStoreConfigT<config::SettingInfo> localFSStoreConfigDescriptions = {
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
static LocalFSStoreConfigT<config::JustValue> localFSStoreConfigDefaults(
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

static LocalFSStoreConfigT<config::JustValue> localFSStoreConfigApplyParse(
    const Path & storeDir,
    LocalFSStoreConfigT<config::OptValue> parsed)
{
    auto defaults = localFSStoreConfigDefaults(
        storeDir,
        parsed.rootDir.optValue.value_or(std::nullopt));
    return {LOCAL_FS_STORE_CONFIG_FIELDS(APPLY_ROW)};
}

LocalFSStore::Config::LocalFSStoreConfig(
    const Store::Config & storeConfig,
    const StoreReference::Params & params)
    : LocalFSStoreConfigT<config::JustValue>{
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
    : LocalFSStoreConfigT<config::JustValue>{
        localFSStoreConfigApplyParse(
            storeConfig.storeDir,
            applyAuthority(
                localFSStoreConfigParse(params),
                rootDir))}
    , storeConfig{storeConfig}
{
}

config::SettingDescriptionMap LocalFSStoreConfig::descriptions()
{
    constexpr auto & descriptions = localFSStoreConfigDescriptions;
    auto defaults = localFSStoreConfigDefaults(settings.nixStore, std::nullopt);
    return {
        LOCAL_FS_STORE_CONFIG_FIELDS(DESC_ROW)
    };
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
