#include "local-binary-cache-store.hh"

namespace nix {

LocalBinaryCacheStore::LocalBinaryCacheStore(std::shared_ptr<Store> localStore,
    const Path & secretKeyFile, const Path & publicKeyFile,
    const Path & binaryCacheDir)
    : BinaryCacheStore(localStore, secretKeyFile, publicKeyFile)
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

std::string LocalBinaryCacheStore::getFile(const std::string & path)
{
    return readFile(binaryCacheDir + "/" + path);
}

static RegisterStoreImplementation regStore([](const std::string & uri) -> std::shared_ptr<Store> {
    if (std::string(uri, 0, 7) != "file://") return 0;
    auto store = std::make_shared<LocalBinaryCacheStore>(std::shared_ptr<Store>(0),
        "", "", // FIXME: allow the signing key to be set
        std::string(uri, 7));
    store->init();
    return store;
});

}
