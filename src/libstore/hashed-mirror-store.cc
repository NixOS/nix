#include "binary-cache-store.hh"
#include "filetransfer.hh"
#include "globals.hh"
#include "archive.hh"

namespace nix {

class HashedMirrorStore : public Store
{
private:

    Path cacheUri;
    Path cacheDir;

public:

    HashedMirrorStore(
        const Params & params, const Path & _cacheUri)
        : Store(params)
        , cacheUri(_cacheUri)
    {
        if (hasPrefix(cacheUri, "hashed-mirror+"))
            cacheUri = cacheUri.substr(14);

        if (cacheUri.back() == '/')
            cacheUri.pop_back();

        if (!hasPrefix(cacheUri, "file://"))
            throw Error("only file:// cache is currently supported in hashed mirror store");

        cacheDir = cacheUri.substr(7);
    }

    std::string getUri() override
    {
        return cacheUri;
    }

    void init()
    {
    }

    void narFromPath(const StorePath & storePath, Sink & sink, const std::string ca) override
    {
        dumpPath(cacheDir + getPath(ca), sink);
    }

    static Hash getHash(std::string ca)
    {
        if (ca == "")
            throw Error("ca cannot be empty in hashed mirror store");

        if (!hasPrefix(ca, "fixed:"))
            throw Error("hashed mirror must be fixed-output");

        ca = ca.substr(6);

        if (hasPrefix(ca, "r:"))
            throw Error("hashed mirror cannot be recursive");

        return Hash(ca);
    }

    static std::string getPath(std::string ca)
    {
        Hash h = getHash(ca);

        return "/" + printHashType(h.type) + "/" + h.to_string(Base16, false);
    }

    bool isValidPathUncached(const StorePath & storePath, std::string ca) override
    {
        return fileExists(getPath(ca));
    }

    void queryPathInfoUncached(const StorePath & path,
        Callback<std::shared_ptr<const ValidPathInfo>> callback, const std::string ca) noexcept override
    {
        auto info = std::make_shared<ValidPathInfo>(path.clone());

        // not efficient!
        StringSink sink;
        dumpPath(cacheDir + getPath(ca), sink);
        info->narHash = hashString(htSHA256, *sink.s);
        info->narSize = sink.s->size();

        info->ca = ca;
        callback(std::move(info));
    }

    std::optional<StorePath> queryPathFromHashPart(const std::string & hashPart) override
    {
        unsupported("queryPathFromHashPart");
    }

    void addToStore(const ValidPathInfo & info, Source & source,
        RepairFlag repair, CheckSigsFlag checkSigs, std::shared_ptr<FSAccessor> accessor) override
    {
        if (info.references.size() > 0)
            throw Error("references are not supported in a hashed mirror store");

        auto dirname = std::string(dirOf(cacheDir + getPath(info.ca)));
        if (!pathExists(dirname))
            if (mkdir(dirname.c_str(), 0777) == -1)
                throw SysError(format("creating directory '%1%'") % dirname);

        auto path = cacheDir + getPath(info.ca);
        restorePath(path, source);

        Hash h(info.ca.substr(6));

        HashSink hashSink(h.type);
        readFile(path, hashSink);

        Hash gotHash = hashSink.finish().first;
        if (gotHash != h)
            throw Error("path '%s' does not have correct hash: expected %s, got %s", path, h.to_string(), gotHash.to_string());
    }

    StorePath addToStore(const string & name, const Path & srcPath,
        FileIngestionMethod method, HashType hashAlgo,
        PathFilter & filter, RepairFlag repair) override
    {
        unsupported("addToStore");
    }

    void ensurePath(const StorePath & path) override
    {
        unsupported("ensurePath");
    }

    StorePath addTextToStore(const string & name, const string & s,
        const StorePathSet & references, RepairFlag repair = NoRepair) override
    { unsupported("addTextToStore"); }

    BuildResult buildDerivation(const StorePath & drvPath, const BasicDerivation & drv,
        BuildMode buildMode = bmNormal) override
    { unsupported("buildDerivation"); }

    StorePathSet queryValidPaths(const StorePathSet & paths, SubstituteFlag maybeSubstitute, std::map<std::string, std::string> pathsInfo) override
    {
        StorePathSet res;
        for (auto & i : paths) {
            auto ca = pathsInfo.find(printStorePath(i));
            if (isValidPath(i, ca != pathsInfo.end() ? ca->second : ""))
                res.insert(i.clone());
        }
        return res;
    }

    // Taken from local-binary-cache.cc, should make this also support http

    static void atomicWrite(const Path & path, const std::string & s)
    {
        Path tmp = path + ".tmp." + std::to_string(getpid());
        AutoDelete del(tmp, false);
        writeFile(tmp, s);
        if (rename(tmp.c_str(), path.c_str()))
            throw SysError(format("renaming '%1%' to '%2%'") % tmp % path);
        del.cancel();
    }

    bool fileExists(const std::string & path)
    {
        return pathExists(cacheDir + "/" + path);
    }

    void upsertFile(const std::string & path,
        const std::string & data,
        const std::string & mimeType)
    {
        atomicWrite(cacheDir + "/" + path, data);
    }

    void getFile(const std::string & path, Sink & sink)
    {
        try {
            readFile(cacheDir + "/" + path, sink);
        } catch (SysError & e) {
            if (e.errNo == ENOENT)
                throw NoSuchBinaryCacheFile("file '%s' does not exist in binary cache", path);
        }
    }

};

static RegisterStoreImplementation regStore([](
    const std::string & uri, const Store::Params & params)
    -> std::shared_ptr<Store>
{
    if (std::string(uri, 0, 14) != "hashed-mirror+")
        return 0;
    auto store = std::make_shared<HashedMirrorStore>(params, uri);
    store->init();
    return store;
});

}
