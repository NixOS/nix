#include "nix/store/local-binary-cache-store.hh"
#include "nix/store/globals.hh"
#include "nix/store/nar-info-disk-cache.hh"
#include "nix/util/signals.hh"
#include "nix/store/store-registration.hh"

#include <atomic>

namespace nix {

LocalBinaryCacheStoreConfig::LocalBinaryCacheStoreConfig(
    std::string_view scheme, PathView binaryCacheDir, const StoreReference::Params & params)
    : Store::Config{params}
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
                .authority = binaryCacheDir,
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
        auto path2 = config->binaryCacheDir + "/" + path;
        static std::atomic<int> counter{0};
        Path tmp = fmt("%s.tmp.%d.%d", path2, getpid(), ++counter);
        AutoDelete del(tmp, false);
        writeFile(tmp, source);
        std::filesystem::rename(tmp, path2);
        del.cancel();
    }

    void getFile(const std::string & path, Sink & sink) override
    {
        try {
            readFile(config->binaryCacheDir + "/" + path, sink);
        } catch (SysError & e) {
            if (e.errNo == ENOENT)
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
    createDirs(config->binaryCacheDir + "/nar");
    createDirs(config->binaryCacheDir + "/" + realisationsPrefix);
    if (config->writeDebugInfo)
        createDirs(config->binaryCacheDir + "/debuginfo");
    createDirs(config->binaryCacheDir + "/log");
    BinaryCacheStore::init();
}

bool LocalBinaryCacheStore::fileExists(const std::string & path)
{
    return pathExists(config->binaryCacheDir + "/" + path);
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
