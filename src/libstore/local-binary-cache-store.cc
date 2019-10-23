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

    void getFile(const std::string & path, Sink & sink) override
    {
        try {
            readFile(binaryCacheDir + "/" + path, sink);
        } catch (PosixError & e) {
            if (e.errNo == ENOENT)
                throw NoSuchBinaryCacheFile("file '%s' does not exist in binary cache", path);
#ifdef _WIN32
        } catch (WinError & e) {
            if (e.lastError == ERROR_FILE_NOT_FOUND)
                throw NoSuchBinaryCacheFile("file '%s' does not exist in binary cache", path);
#endif
        }
    }

    PathSet queryAllValidPaths() override
    {
        PathSet paths;

        for (auto & entry : readDirectory(binaryCacheDir)) {
            auto name = entry.name();
            if (name.size() != 40 ||
                !hasSuffix(name, ".narinfo"))
                continue;
            paths.insert(storeDir + "/" + name.substr(0, name.size() - 8));
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
#ifndef _WIN32
    Path tmp = path + ".tmp." + std::to_string(getpid());
    AutoDelete del(tmp, false);
    writeFile(tmp, s);
    if (rename(tmp.c_str(), path.c_str()))
        throw PosixError(format("renaming '%1%' to '%2%'") % tmp % path);
#else
    Path tmp = path + ".tmp." + std::to_string(GetCurrentProcessId());
    AutoDelete del(tmp, false);
    writeFile(tmp, s);
    if (!MoveFileExW(pathW(tmp).c_str(), pathW(path).c_str(), MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
        throw WinError("MoveFileExW in atomicWrite '%1%' to '%2%'", tmp, path);
#endif
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
