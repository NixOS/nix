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
        const Params & params, const Path & binaryCacheDir)
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

    bool fileExists(const std::string & path) override;

    void upsertFile(const std::string & path,
        const std::string & data,
        const std::string & mimeType) override;

    void getFile(const std::string & path,
        std::function<void(std::shared_ptr<std::string>)> success,
        std::function<void(std::exception_ptr exc)> failure) override
    {
        sync2async<std::shared_ptr<std::string>>(success, failure, [&]() {
            try {
                return std::make_shared<std::string>(readFile(binaryCacheDir + "/" + path));
            } catch (SysError & e) {
                if (e.errNo == ENOENT) return std::shared_ptr<std::string>();
                throw;
            }
        });
    }

    PathSet queryAllValidPaths() override
    {
        PathSet paths;

        for (auto & entry : readDirectory(binaryCacheDir)) {
            if (entry.name.size() != 40 ||
                !hasSuffix(entry.name, ".narinfo"))
                continue;
            paths.insert(storeDir + "/" + entry.name.substr(0, entry.name.size() - 8));
        }

        return paths;
    }

};

void LocalBinaryCacheStore::init()
{
    createDirs(binaryCacheDir + "/nar");
    BinaryCacheStore::init();
}

static void atomicWrite(const Path & path, const std::string & s)
{
    Path tmp = path + ".tmp." + std::to_string(getpid());
    AutoDelete del(tmp, false);
    writeFile(tmp, s);
    if (rename(tmp.c_str(), path.c_str()))
        throw SysError(format("renaming '%1%' to '%2%'") % tmp % path);
    del.cancel();
}

bool LocalBinaryCacheStore::fileExists(const std::string & path)
{
    return pathExists(binaryCacheDir + "/" + path);
}

void LocalBinaryCacheStore::upsertFile(const std::string & path,
    const std::string & data,
    const std::string & mimeType)
{
    atomicWrite(binaryCacheDir + "/" + path, data);
}

static RegisterStoreImplementation regStore([](
    const std::string & uri, const Store::Params & params)
    -> std::shared_ptr<Store>
{
    if (getEnv("_NIX_FORCE_HTTP_BINARY_CACHE_STORE") == "1" ||
        std::string(uri, 0, 7) != "file://")
        return 0;
    auto store = std::make_shared<LocalBinaryCacheStore>(params, std::string(uri, 7));
    store->init();
    return store;
});

}
