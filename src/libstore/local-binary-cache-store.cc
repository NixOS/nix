#include "binary-cache-store.hh"
#include "globals.hh"
#include "nar-info-disk-cache.hh"

namespace nix {

class LocalBinaryCacheStore : public BinaryCacheStore
{
private:

    Path binaryCacheDir;

public:

    LocalBinaryCacheStore(
        const Params & params, PathView binaryCacheDir)
        : BinaryCacheStore(params)
        , binaryCacheDir(binaryCacheDir)
    {
    }

    void init() override;

    std::string getUri() override
    {
        return "file://" + binaryCacheDir;
    }

protected:

    bool fileExists(std::string_view path) override;

    void upsertFile(std::string_view path,
        std::string_view data,
        std::string_view mimeType) override;

    void getFile(std::string_view path, Sink & sink) override
    {
        try {
            readFile(Path { binaryCacheDir } << "/" << path, sink);
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
            paths.insert(parseStorePath(storeDir + "/" + entry.name.substr(0, entry.name.size() - 8)));
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

static void atomicWrite(PathView path, std::string_view s)
{
    Path tmp = Path { path } << ".tmp." << std::to_string(getpid());
    AutoDelete del(tmp, false);
    writeFile(tmp, s);
    if (rename(Path { tmp }.c_str(), Path { path }.c_str()))
        throw SysError("renaming '%1%' to '%2%'", tmp, path);
    del.cancel();
}

bool LocalBinaryCacheStore::fileExists(std::string_view path)
{
    return pathExists(Path { binaryCacheDir } << "/" << path);
}

void LocalBinaryCacheStore::upsertFile(std::string_view path,
    std::string_view data,
    std::string_view mimeType)
{
    atomicWrite(Path { binaryCacheDir } << "/" << path, data);
}

static RegisterStoreImplementation regStore([](
    std::string_view uri, const Store::Params & params)
    -> std::shared_ptr<Store>
{
    if (getEnv("_NIX_FORCE_HTTP_BINARY_CACHE_STORE") == "1" ||
        std::string(uri, 0, 7) != "file://")
        return 0;
    auto store = std::make_shared<LocalBinaryCacheStore>(params, uri.substr(7));
    store->init();
    return store;
});

}
