#include "binary-cache-store.hh"
#include "globals.hh"

namespace nix {

class LocalBinaryCacheStore : public BinaryCacheStore
{
private:

    Path binaryCacheDir;

public:

    LocalBinaryCacheStore(std::shared_ptr<Store> localStore,
        const StoreParams & params, const Path & binaryCacheDir)
        : BinaryCacheStore(localStore, params)
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

    void upsertFile(const std::string & path, const std::string & data) override;

    std::shared_ptr<std::string> getFile(const std::string & path) override;

    PathSet queryAllValidPaths() override
    {
        PathSet paths;

        for (auto & entry : readDirectory(binaryCacheDir)) {
            if (entry.name.size() != 40 ||
                !hasSuffix(entry.name, ".narinfo"))
                continue;
            paths.insert(settings.nixStore + "/" + entry.name.substr(0, entry.name.size() - 8));
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
        throw SysError(format("renaming ‘%1%’ to ‘%2%’") % tmp % path);
    del.cancel();
}

bool LocalBinaryCacheStore::fileExists(const std::string & path)
{
    return pathExists(binaryCacheDir + "/" + path);
}

void LocalBinaryCacheStore::upsertFile(const std::string & path, const std::string & data)
{
    atomicWrite(binaryCacheDir + "/" + path, data);
}

std::shared_ptr<std::string> LocalBinaryCacheStore::getFile(const std::string & path)
{
    try {
        return std::make_shared<std::string>(readFile(binaryCacheDir + "/" + path));
    } catch (SysError & e) {
        if (e.errNo == ENOENT) return 0;
        throw;
    }
}

static RegisterStoreImplementation regStore([](
    const std::string & uri, const StoreParams & params)
    -> std::shared_ptr<Store>
{
    if (std::string(uri, 0, 7) != "file://") return 0;
    auto store = std::make_shared<LocalBinaryCacheStore>(
        std::shared_ptr<Store>(0), params, std::string(uri, 7));
    store->init();
    return store;
});

}
