#pragma once

#include "crypto.hh"
#include "store-api.hh"

#include "pool.hh"

#include <atomic>

namespace nix {

struct NarInfo;

class BinaryCacheStore : public Store
{
private:

    std::unique_ptr<SecretKey> secretKey;

    std::shared_ptr<Store> localStore;

    std::string compression;

protected:

    BinaryCacheStore(std::shared_ptr<Store> localStore,
        const StoreParams & params);

    [[noreturn]] void notImpl();

    virtual bool fileExists(const std::string & path) = 0;

    virtual void upsertFile(const std::string & path, const std::string & data) = 0;

    /* Return the contents of the specified file, or null if it
       doesn't exist. */
    virtual std::shared_ptr<std::string> getFile(const std::string & path) = 0;

public:

    virtual void init();

private:

    std::string narMagic;

    std::string narInfoFileFor(const Path & storePath);

public:

    bool isValidPathUncached(const Path & path) override;

    PathSet queryValidPaths(const PathSet & paths) override
    { notImpl(); }

    PathSet queryAllValidPaths() override
    { notImpl(); }

    std::shared_ptr<ValidPathInfo> queryPathInfoUncached(const Path & path) override;

    void queryReferrers(const Path & path,
        PathSet & referrers) override
    { notImpl(); }

    PathSet queryValidDerivers(const Path & path) override
    { return {}; }

    PathSet queryDerivationOutputs(const Path & path) override
    { notImpl(); }

    StringSet queryDerivationOutputNames(const Path & path) override
    { notImpl(); }

    Path queryPathFromHashPart(const string & hashPart) override
    { notImpl(); }

    PathSet querySubstitutablePaths(const PathSet & paths) override
    { return {}; }

    void querySubstitutablePathInfos(const PathSet & paths,
        SubstitutablePathInfos & infos) override;

    void addToStore(const ValidPathInfo & info, const std::string & nar,
        bool repair = false) override;

    Path addToStore(const string & name, const Path & srcPath,
        bool recursive = true, HashType hashAlgo = htSHA256,
        PathFilter & filter = defaultPathFilter, bool repair = false) override;

    Path addTextToStore(const string & name, const string & s,
        const PathSet & references, bool repair = false) override;

    void narFromPath(const Path & path, Sink & sink) override;

    void buildPaths(const PathSet & paths, BuildMode buildMode = bmNormal) override;

    BuildResult buildDerivation(const Path & drvPath, const BasicDerivation & drv,
        BuildMode buildMode = bmNormal) override
    { notImpl(); }

    void ensurePath(const Path & path) override;

    void addTempRoot(const Path & path) override
    { notImpl(); }

    void addIndirectRoot(const Path & path) override
    { notImpl(); }

    void syncWithGC() override
    { }

    Roots findRoots() override
    { notImpl(); }

    void collectGarbage(const GCOptions & options, GCResults & results) override
    { notImpl(); }

    void optimiseStore() override
    { }

    bool verifyStore(bool checkContents, bool repair) override
    { return true; }

    ref<FSAccessor> getFSAccessor() override;

    void addSignatures(const Path & storePath, const StringSet & sigs)
    { notImpl(); }

};

}
