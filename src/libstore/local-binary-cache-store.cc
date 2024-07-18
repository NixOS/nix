#include "local-binary-cache-store.hh"
#include "globals.hh"
#include "nar-info-disk-cache.hh"
#include "signals.hh"

#include <atomic>

namespace nix {

LocalBinaryCacheStoreConfig::LocalBinaryCacheStoreConfig(
    std::string_view scheme,
    PathView binaryCacheDir,
    const Params & params)
    : StoreConfig(params)
    , BinaryCacheStoreConfig(params)
    , binaryCacheDir(binaryCacheDir)
{
}


std::string LocalBinaryCacheStoreConfig::doc()
{
    return
      #include "local-binary-cache-store.md"
      ;
}


struct LocalBinaryCacheStore : virtual LocalBinaryCacheStoreConfig, virtual BinaryCacheStore
{
    /**
     * @param binaryCacheDir `file://` is a short-hand for `file:///`
     * for now.
     */
    LocalBinaryCacheStore(
        std::string_view scheme,
        PathView binaryCacheDir,
        const Params & params)
        : StoreConfig(params)
        , BinaryCacheStoreConfig(params)
        , LocalBinaryCacheStoreConfig(scheme, binaryCacheDir, params)
        , Store(params)
        , BinaryCacheStore(params)
    {
    }

    void init() override;

    std::string getUri() override
    {
        return "file://" + binaryCacheDir;
    }

protected:

    bool fileExists(const std::string & path) override;

    void upsertFile(const std::string & path,
        std::shared_ptr<std::basic_iostream<char>> istream,
        const std::string & mimeType) override
    {
        auto path2 = binaryCacheDir + "/" + path;
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
            readFile(binaryCacheDir + "/" + path, sink);
        } catch (SysError & e) {
            if (e.errNo == ENOENT)
                throw NoSuchBinaryCacheFile("file '%s' does not exist in binary cache", path);
            throw;
        }
    }

    StorePathSet queryAllValidPaths() override
    {
        StorePathSet paths;

        for (auto & entry : std::filesystem::directory_iterator{binaryCacheDir}) {
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
    createDirs(binaryCacheDir + "/nar");
    createDirs(binaryCacheDir + "/" + realisationsPrefix);
    if (writeDebugInfo)
        createDirs(binaryCacheDir + "/debuginfo");
    createDirs(binaryCacheDir + "/log");
    BinaryCacheStore::init();
}

bool LocalBinaryCacheStore::fileExists(const std::string & path)
{
    return pathExists(binaryCacheDir + "/" + path);
}

std::set<std::string> LocalBinaryCacheStoreConfig::uriSchemes()
{
    if (getEnv("_NIX_FORCE_HTTP") == "1")
        return {};
    else
        return {"file"};
}

static RegisterStoreImplementation<LocalBinaryCacheStore, LocalBinaryCacheStoreConfig> regLocalBinaryCacheStore;

}
