#include "nix/store/local-binary-cache-store.hh"
#include "nix/store/globals.hh"
#include "nix/store/nar-info-disk-cache.hh"
#include "nix/util/signals.hh"
#include "nix/store/store-registration.hh"

#include <atomic>

namespace nix {

config::SettingDescriptionMap LocalBinaryCacheStoreConfig::descriptions()
{
    config::SettingDescriptionMap ret;
    ret.merge(StoreConfig::descriptions());
    ret.merge(BinaryCacheStoreConfig::descriptions());
    return ret;
}


LocalBinaryCacheStoreConfig::LocalBinaryCacheStoreConfig(
    std::string_view scheme,
    PathView binaryCacheDir,
    const StoreReference::Params & params)
    : Store::Config{params}
    , BinaryCacheStoreConfig{*this, params}
    , binaryCacheDir(binaryCacheDir)
{
}


std::string LocalBinaryCacheStoreConfig::doc()
{
    return
      #include "local-binary-cache-store.md"
      ;
}


struct LocalBinaryCacheStore :
    virtual BinaryCacheStore
{
    using Config = LocalBinaryCacheStoreConfig;

    ref<const Config> config;

    LocalBinaryCacheStore(ref<const Config> config)
        : Store{*config}
        , BinaryCacheStore{*config}
        , config{config}
    {
        init();
    }

    void init() override;

    std::string getUri() override
    {
        return "file://" + config->binaryCacheDir;
    }

protected:

    bool fileExists(const std::string & path) override;

    void upsertFile(const std::string & path,
        std::shared_ptr<std::basic_iostream<char>> istream,
        const std::string & mimeType) override
    {
        auto path2 = config->binaryCacheDir + "/" + path;
        static std::atomic<int> counter{0};
        Path tmp = fmt("%s.tmp.%d.%d", path2, getpid(), ++counter);
        AutoDelete del(tmp, false);
        StreamToSourceAdapter source(istream);
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

        for (auto & entry : std::filesystem::directory_iterator{config->binaryCacheDir}) {
            checkInterrupt();
            auto name = entry.path().filename().string();
            if (name.size() != 40 ||
                !hasSuffix(name, ".narinfo"))
                continue;
            paths.insert(parseStorePath(
                    storeDir + "/" + name.substr(0, name.size() - 8)
                    + "-" + MissingName));
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

std::set<std::string> LocalBinaryCacheStoreConfig::uriSchemes()
{
    if (getEnv("_NIX_FORCE_HTTP") == "1")
        return {};
    else
        return {"file"};
}

ref<Store> LocalBinaryCacheStoreConfig::openStore() const {
    return make_ref<LocalBinaryCacheStore>(ref{shared_from_this()});
}

static RegisterStoreImplementation<LocalBinaryCacheStore::Config> regLocalBinaryCacheStore;

}
