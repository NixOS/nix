#include "nix/store/local-binary-cache-store.hh"
#include "nix/store/globals.hh"
#include "nix/store/nar-info-disk-cache.hh"
#include "nix/util/signals.hh"
#include "nix/store/store-registration.hh"

#include <atomic>

namespace nix {

static std::filesystem::path checkBinaryCachePath(const std::filesystem::path & root, const std::string & path)
{
    auto p = std::filesystem::path(requireCString(path));
    if (p.empty())
        throw Error("local binary cache path must not be empty");

    if (p.is_absolute())
        throw Error("local binary cache path '%s' must not be absolute", path);

    for (const auto & segment : p) {
        if (segment.native() == OS_STR("..") || segment.native() == OS_STR("."))
            throw Error("local binary cache path '%s' must not contain '..' or '.' segments", path);
    }

    return root / p.relative_path();
}

LocalBinaryCacheStoreConfig::LocalBinaryCacheStoreConfig(
    const std::filesystem::path & binaryCacheDir, const StoreReference::Params & params)
    : Store::Config{params, FilePathType::Native}
    , BinaryCacheStoreConfig{params}
    , binaryCacheDir(binaryCacheDir)
{
}

std::string LocalBinaryCacheStoreConfig::doc()
{
    return
#include "local-binary-cache-store.md"
        ;
}

StoreReference LocalBinaryCacheStoreConfig::getReference() const
{
    return {
        .variant =
            StoreReference::Specified{
                .scheme = "file",
                .authority = encodeUrlPath(pathToUrlPath(binaryCacheDir)),
            },
    };
}

struct LocalBinaryCacheStore : virtual BinaryCacheStore
{
    using Config = LocalBinaryCacheStoreConfig;

    ref<Config> config;

    LocalBinaryCacheStore(ref<Config> config)
        : Store{*config}
        , BinaryCacheStore{*config}
        , config{config}
    {
    }

    void init() override;

protected:

    bool fileExists(const std::string & path) override;

    void upsertFile(
        const std::string & path, RestartableSource & source, const std::string & mimeType, uint64_t sizeHint) override
    {
        auto path2 = checkBinaryCachePath(config->binaryCacheDir, path);
        static std::atomic<int> counter{0};
        createDirs(path2.parent_path());
        auto tmp = path2;
        tmp += fmt(".tmp.%d.%d", getpid(), ++counter);
        AutoDelete del(tmp, false);
        writeFile(tmp, source); /* TODO: Don't follow symlinks? */
        std::filesystem::rename(tmp, path2);
        del.cancel();
    }

    void getFile(const std::string & path, Sink & sink) override
    {
        try {
            /* TODO: Don't follow symlinks? */
            readFile(checkBinaryCachePath(config->binaryCacheDir, path), sink);
        } catch (SystemError & e) {
            if (e.is(std::errc::no_such_file_or_directory))
                throw NoSuchBinaryCacheFile("file '%s' does not exist in binary cache", path);
            throw;
        }
    }

    StorePathSet queryAllValidPaths() override
    {
        StorePathSet paths;

        for (auto & entry : DirectoryIterator{config->binaryCacheDir}) {
            checkInterrupt();
            auto name = entry.path().filename().string();
            if (name.size() != 40 || !hasSuffix(name, ".narinfo"))
                continue;
            paths.insert(parseStorePath(storeDir + "/" + name.substr(0, name.size() - 8) + "-" + MissingName));
        }

        return paths;
    }

    std::optional<TrustedFlag> isTrustedClient() override
    {
        return Trusted;
    }
};

void LocalBinaryCacheStore::init()
{
    createDirs(config->binaryCacheDir / "nar");
    createDirs(config->binaryCacheDir / realisationsPrefix);
    if (config->writeDebugInfo)
        createDirs(config->binaryCacheDir / "debuginfo");
    createDirs(config->binaryCacheDir / "log");
    BinaryCacheStore::init();
}

bool LocalBinaryCacheStore::fileExists(const std::string & path)
{
    return pathExists(checkBinaryCachePath(config->binaryCacheDir, path));
}

StringSet LocalBinaryCacheStoreConfig::uriSchemes()
{
    if (getEnv("_NIX_FORCE_HTTP") == "1")
        return {};
    else
        return {"file"};
}

ref<Store> LocalBinaryCacheStoreConfig::openStore() const
{
    auto store = make_ref<LocalBinaryCacheStore>(
        ref{// FIXME we shouldn't actually need a mutable config
            std::const_pointer_cast<LocalBinaryCacheStore::Config>(shared_from_this())});
    store->init();
    return store;
}

static RegisterStoreImplementation<LocalBinaryCacheStore::Config> regLocalBinaryCacheStore;

} // namespace nix
