#include "binary-cache-store.hh"
#include "globals.hh"
#include "nar-info-disk-cache.hh"

namespace nix {

struct LocalBinaryCacheStoreConfig : virtual BinaryCacheStoreConfig
{
    using BinaryCacheStoreConfig::BinaryCacheStoreConfig;
};

class LocalBinaryCacheStore : public BinaryCacheStore, public virtual LocalBinaryCacheStoreConfig
{
private:

    Path binaryCacheDir;

public:

    LocalBinaryCacheStore(
        const Path & binaryCacheDir,
        const Params & params)
        : LocalBinaryCacheStoreConfig(params)
        , BinaryCacheStore(params)
        , binaryCacheDir(binaryCacheDir)
    {
    }

    void init() override;

    std::string getUri() override
    {
        return "file://" + binaryCacheDir;
    }

    static std::vector<std::string> uriPrefixes();

protected:

    bool fileExists(const std::string & path) override;

    void upsertFile(const std::string & path,
        std::shared_ptr<std::basic_iostream<char>> istream,
        const std::string & mimeType) override
    {
        auto path2 = binaryCacheDir + "/" + path;
        Path tmp = path2 + ".tmp." + std::to_string(getpid());
        AutoDelete del(tmp, false);
        StreamToSourceAdapter source(istream);
        writeFile(tmp, source);
        if (rename(tmp.c_str(), path2.c_str()))
            throw SysError("renaming '%1%' to '%2%'", tmp, path2);
        del.cancel();
    }

    void getFile(const std::string & path, Sink & sink) override
    {
        try {
            readFile(binaryCacheDir + "/" + path, sink);
        } catch (SysError & e) {
            if (e.errNo == ENOENT)
                throw NoSuchBinaryCacheFile("file '%s' does not exist in binary cache", path);
        }
    }

    StorePathSet queryAllValidPaths() override
    {
        StorePathSet paths;

        for (auto & entry : readDirectory(binaryCacheDir)) {
            if (entry.name.size() != 40 ||
                !hasSuffix(entry.name, ".narinfo"))
                continue;
            paths.insert(parseStorePath(
                    storeDir + "/" + entry.name.substr(0, entry.name.size() - 8)
                    + "-" + MissingName));
        }

        return paths;
    }

};

void LocalBinaryCacheStore::init()
{
    createDirs(binaryCacheDir + "/nar");
    if (writeDebugInfo)
        createDirs(binaryCacheDir + "/debuginfo");
    BinaryCacheStore::init();
}

bool LocalBinaryCacheStore::fileExists(const std::string & path)
{
    return pathExists(binaryCacheDir + "/" + path);
}

std::vector<std::string> LocalBinaryCacheStore::uriPrefixes()
{
    if (getEnv("_NIX_FORCE_HTTP_BINARY_CACHE_STORE") == "1")
        return {};
    else
        return {"file"};
}

static RegisterStoreImplementation<LocalBinaryCacheStore, LocalBinaryCacheStoreConfig> regStore;

}
