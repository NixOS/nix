#include "binary-cache-store.hh"
#include "globals.hh"

namespace nix {

class LocalBinaryCacheStore : public BinaryCacheStore
{
private:

    Path binaryCacheDir;

public:

    LocalBinaryCacheStore(std::shared_ptr<Store> localStore,
        const Path & secretKeyFile, const Path & binaryCacheDir);

    void init() override;

protected:

    bool fileExists(const std::string & path) override;

    void upsertFile(const std::string & path, const std::string & data) override;

    std::shared_ptr<std::string> getFile(const std::string & path) override;

};

LocalBinaryCacheStore::LocalBinaryCacheStore(std::shared_ptr<Store> localStore,
    const Path & secretKeyFile, const Path & binaryCacheDir)
    : BinaryCacheStore(localStore, secretKeyFile)
    , binaryCacheDir(binaryCacheDir)
{
}

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

ref<Store> openLocalBinaryCacheStore(std::shared_ptr<Store> localStore,
    const Path & secretKeyFile, const Path & binaryCacheDir)
{
    auto store = make_ref<LocalBinaryCacheStore>(
        localStore, secretKeyFile, binaryCacheDir);
    store->init();
    return store;
}

static RegisterStoreImplementation regStore([](const std::string & uri) -> std::shared_ptr<Store> {
    if (std::string(uri, 0, 7) != "file://") return 0;
    return openLocalBinaryCacheStore(std::shared_ptr<Store>(0),
        settings.get("binary-cache-secret-key-file", string("")),
        std::string(uri, 7));
});

}
