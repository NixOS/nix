#include "nix/store/local-binary-cache-store.hh"
#include "nix/util/environment-variables.hh"
#include "nix/util/signals.hh"
#include "nix/store/store-registration.hh"
#include "nix/util/url.hh"

#include <atomic>

namespace nix {

LocalBinaryCacheStoreConfig::LocalBinaryCacheStoreConfig(
    const std::filesystem::path & binaryCacheDir, const StoreReference::Params & params)
    : Store::Config{params, FilePathType::Unix}
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
private:
    void anchor() override;

public:
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

    bool fileExists(const CanonPath & path) override;

    void upsertFile(
        const CanonPath & path, RestartableSource & source, const std::string & mimeType, uint64_t sizeHint) override
    {
        auto path2 = config->binaryCacheDir / path.rel();
        static std::atomic<int> counter{0};
        createDirs(path2.parent_path());
        auto tmp = path2;
        tmp += fmt(".tmp.%d.%d", getpid(), ++counter);
        AutoDelete del(tmp, false);
        writeFile(tmp, source); /* TODO: Don't follow symlinks? */
        std::filesystem::rename(tmp, path2);
        del.cancel();
    }

    void getFile(const ParsedMaybeRelativeURL & url, Sink & sink) override
    {
        std::visit(
            overloaded{
                [&](const ParsedRelativeUrl & url) {
                    if (url.query)
                        throw Error(
                            "local binary cache does not support query parameters in URL '%s', not even empty params map trailing ?",
                            url.to_string());
                    if (!url.fragment.empty())
                        throw Error("local binary cache does not support fragment in URL '%s'", url.to_string());
                    auto path = urlPathToPath(url.path);
                    if (path.is_absolute())
                        throw Error("local binary cache does not support absolute path in URL '%s'", url.to_string());
                    try {
                        /* TODO: Don't follow symlinks? */
                        readFile(config->binaryCacheDir / path, sink);
                    } catch (SystemError & e) {
                        if (e.is(std::errc::no_such_file_or_directory))
                            throw NoSuchBinaryCacheFile("file %s does not exist in binary cache", PathFmt(path));
                        throw;
                    }
                },
                [&](const ParsedURL &) { unsupported("getFile from absolute URL"); },
            },
            url);
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
    createDirs(config->binaryCacheDir / realisationsPrefix.rel());
    if (config->writeDebugInfo)
        createDirs(config->binaryCacheDir / "debuginfo");
    createDirs(config->binaryCacheDir / "log");
    BinaryCacheStore::init();
}

bool LocalBinaryCacheStore::fileExists(const CanonPath & path)
{
    return pathExists(config->binaryCacheDir / path.rel());
}

StringSet LocalBinaryCacheStoreConfig::uriSchemes()
{
    if (getEnv("_NIX_FORCE_HTTP") == "1")
        return {};
    else
        return {"file"};
}

void LocalBinaryCacheStoreConfig::anchor() {}

void LocalBinaryCacheStore::anchor() {}

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
