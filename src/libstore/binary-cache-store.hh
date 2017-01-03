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

    std::string compression;

    bool writeNARListing;

    bool publishToIPFS;

protected:

    BinaryCacheStore(const Params & params);

    [[noreturn]] void notImpl();

public:

    virtual bool fileExists(const std::string & path) = 0;

    virtual void upsertFile(const std::string & path, const std::string & data) = 0;

    /* Return the contents of the specified file, or null if it
       doesn't exist. */
    virtual void getFile(const std::string & path,
        std::function<void(std::shared_ptr<std::string>)> success,
        std::function<void(std::exception_ptr exc)> failure) = 0;

    std::shared_ptr<std::string> getFile(const std::string & path);

protected:

    bool wantMassQuery_ = false;
    int priority = 50;

public:

    virtual void init();

private:

    std::string narMagic;

    std::string narInfoFileFor(const Path & storePath);

public:

    bool isValidPathUncached(const Path & path) override;

    PathSet queryAllValidPaths() override
    { notImpl(); }

    void queryPathInfoUncached(const Path & path,
        std::function<void(std::shared_ptr<ValidPathInfo>)> success,
        std::function<void(std::exception_ptr exc)> failure) override;

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
        SubstitutablePathInfos & infos) override
    { }

    bool wantMassQuery() override { return wantMassQuery_; }

    void addToStore(const ValidPathInfo & info, const ref<std::string> & nar,
        bool repair, bool dontCheckSigs,
        std::shared_ptr<FSAccessor> accessor) override;

    Path addToStore(const string & name, const Path & srcPath,
        bool recursive, HashType hashAlgo,
        PathFilter & filter, bool repair) override;

    Path addTextToStore(const string & name, const string & s,
        const PathSet & references, bool repair) override;

    void narFromPath(const Path & path, Sink & sink) override;

    void buildPaths(const PathSet & paths, BuildMode buildMode) override
    { notImpl(); }

    BuildResult buildDerivation(const Path & drvPath, const BasicDerivation & drv,
        BuildMode buildMode) override
    { notImpl(); }

    void ensurePath(const Path & path) override
    { notImpl(); }

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

public:

    void addSignatures(const Path & storePath, const StringSet & sigs) override
    { notImpl(); }

};

}
