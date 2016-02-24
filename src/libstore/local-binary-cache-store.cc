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

}
