#pragma once

#include "crypto.hh"
#include "store-api.hh"

#include "lru-cache.hh"
#include "sync.hh"
#include "pool.hh"

#include <atomic>

namespace nix {

struct NarInfo;

class BinaryCacheStore : public Store
{
private:

    std::unique_ptr<SecretKey> secretKey;
    std::unique_ptr<PublicKeys> publicKeys;

    std::shared_ptr<Store> localStore;

    struct State
    {
        LRUCache<Path, ref<NarInfo>> narInfoCache{32 * 1024};
    };

    Sync<State> state;

protected:

    BinaryCacheStore(std::shared_ptr<Store> localStore, const Path & secretKeyFile);

    [[noreturn]] void notImpl();

    virtual bool fileExists(const std::string & path) = 0;

    virtual void upsertFile(const std::string & path, const std::string & data) = 0;

    virtual std::string getFile(const std::string & path) = 0;

public:

    virtual void init();

    struct Stats
    {
        std::atomic<uint64_t> narInfoRead{0};
        std::atomic<uint64_t> narInfoReadAverted{0};
        std::atomic<uint64_t> narInfoWrite{0};
        std::atomic<uint64_t> narInfoCacheSize{0};
        std::atomic<uint64_t> narRead{0};
        std::atomic<uint64_t> narReadBytes{0};
        std::atomic<uint64_t> narReadCompressedBytes{0};
        std::atomic<uint64_t> narWrite{0};
        std::atomic<uint64_t> narWriteAverted{0};
        std::atomic<uint64_t> narWriteBytes{0};
        std::atomic<uint64_t> narWriteCompressedBytes{0};
        std::atomic<uint64_t> narWriteCompressionTimeMs{0};
    };

    const Stats & getStats();

private:

    Stats stats;

    std::string narMagic;

    std::string narInfoFileFor(const Path & storePath);

    void addToCache(const ValidPathInfo & info, const string & nar);

protected:

    NarInfo readNarInfo(const Path & storePath);

public:

    bool isValidPath(const Path & path) override;

    PathSet queryValidPaths(const PathSet & paths) override
    { notImpl(); }

    PathSet queryAllValidPaths() override
    { notImpl(); }

    ValidPathInfo queryPathInfo(const Path & path) override;

    Hash queryPathHash(const Path & path) override
    { notImpl(); }

    void queryReferrers(const Path & path,
        PathSet & referrers) override
    { notImpl(); }

    Path queryDeriver(const Path & path) override
    { return ""; }

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

    Path addToStore(const string & name, const Path & srcPath,
        bool recursive = true, HashType hashAlgo = htSHA256,
        PathFilter & filter = defaultPathFilter, bool repair = false) override;

    Path addTextToStore(const string & name, const string & s,
        const PathSet & references, bool repair = false) override;

    void narFromPath(const Path & path, Sink & sink) override;

    void exportPath(const Path & path, bool sign, Sink & sink) override;

    Paths importPaths(bool requireSignature, Source & source,
        std::shared_ptr<FSAccessor> accessor) override;

    Path importPath(Source & source, std::shared_ptr<FSAccessor> accessor);

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

    PathSet queryFailedPaths() override
    { return {}; }

    void clearFailedPaths(const PathSet & paths) override
    { }

    void optimiseStore() override
    { }

    bool verifyStore(bool checkContents, bool repair) override
    { return true; }

    ref<FSAccessor> getFSAccessor() override;

    void addSignatures(const Path & storePath, const StringSet & sigs)
    { notImpl(); }

};

}
