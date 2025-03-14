#pragma once
///@file

#include "local-store.hh"
#include "build-result.hh"

namespace nix {

struct BuildResult;

/**
 * A restricted store has a pointer to one of these, which manages the
 * restrictions that are in place.
 *
 * This is a separate data type so the whitelists can be mutated before
 * the restricted store is created: put differently, someones we don't
 * know whether we will in fact create a restricted store, but we need
 * to prepare the whitelists just in case.
 *
 * It is possible there are other ways to solve this problem. This was
 * just the easiest place to begin, when this was extracted from
 * `LocalDerivationGoal`.
 */
struct RestrictionContext
{
    /**
     * Paths that are already allowed to begin with
     */
    virtual const StorePathSet & originalPaths() = 0;

    /**
     * Paths that were added via recursive Nix calls.
     */
    StorePathSet addedPaths;

    /**
     * Realisations that were added via recursive Nix calls.
     */
    std::set<DrvOutput> addedDrvOutputs;

    /**
     * Recursive Nix calls are only allowed to build or realize paths
     * in the original input closure or added via a recursive Nix call
     * (so e.g. you can't do 'nix-store -r /nix/store/<bla>' where
     * /nix/store/<bla> is some arbitrary path in a binary cache).
     */
    virtual bool isAllowed(const StorePath &) = 0;
    virtual bool isAllowed(const DrvOutput & id) = 0;
    bool isAllowed(const DerivedPath & id);

    /**
     * Add 'path' to the set of paths that may be referenced by the
     * outputs, and make it appear in the sandbox.
     */
    virtual void addDependency(const StorePath & path) = 0;
};

struct RestrictedStoreConfig : virtual LocalFSStoreConfig
{
    using LocalFSStoreConfig::LocalFSStoreConfig;
    const std::string name() override
    {
        return "Restricted Store";
    }
};

/**
 * A wrapper around LocalStore that only allows building/querying of
 * paths that are in the input closures of the build or were added via
 * recursive Nix calls.
 */
struct RestrictedStore : public virtual RestrictedStoreConfig, public virtual IndirectRootStore, public virtual GcStore
{
    ref<LocalStore> next;

    RestrictionContext & goal;

    RestrictedStore(const Params & params, ref<LocalStore> next, RestrictionContext & goal)
        : StoreConfig(params)
        , LocalFSStoreConfig(params)
        , RestrictedStoreConfig(params)
        , Store(params)
        , LocalFSStore(params)
        , next(next)
        , goal(goal)
    {
    }

    Path getRealStoreDir() override
    {
        return next->realStoreDir;
    }

    std::string getUri() override
    {
        return next->getUri();
    }

    StorePathSet queryAllValidPaths() override;

    void queryPathInfoUncached(
        const StorePath & path, Callback<std::shared_ptr<const ValidPathInfo>> callback) noexcept override;

    void queryReferrers(const StorePath & path, StorePathSet & referrers) override;

    std::map<std::string, std::optional<StorePath>>
    queryPartialDerivationOutputMap(const StorePath & path, Store * evalStore = nullptr) override;

    std::optional<StorePath> queryPathFromHashPart(const std::string & hashPart) override
    {
        throw Error("queryPathFromHashPart");
    }

    StorePath addToStore(
        std::string_view name,
        const SourcePath & srcPath,
        ContentAddressMethod method,
        HashAlgorithm hashAlgo,
        const StorePathSet & references,
        PathFilter & filter,
        RepairFlag repair) override
    {
        throw Error("addToStore");
    }

    void addToStore(
        const ValidPathInfo & info,
        Source & narSource,
        RepairFlag repair = NoRepair,
        CheckSigsFlag checkSigs = CheckSigs) override;

    StorePath addToStoreFromDump(
        Source & dump,
        std::string_view name,
        FileSerialisationMethod dumpMethod,
        ContentAddressMethod hashMethod,
        HashAlgorithm hashAlgo,
        const StorePathSet & references,
        RepairFlag repair) override;

    void narFromPath(const StorePath & path, Sink & sink) override;

    void ensurePath(const StorePath & path) override;

    void registerDrvOutput(const Realisation & info) override;

    void queryRealisationUncached(
        const DrvOutput & id, Callback<std::shared_ptr<const Realisation>> callback) noexcept override;

    void
    buildPaths(const std::vector<DerivedPath> & paths, BuildMode buildMode, std::shared_ptr<Store> evalStore) override;

    std::vector<KeyedBuildResult> buildPathsWithResults(
        const std::vector<DerivedPath> & paths,
        BuildMode buildMode = bmNormal,
        std::shared_ptr<Store> evalStore = nullptr) override;

    BuildResult
    buildDerivation(const StorePath & drvPath, const BasicDerivation & drv, BuildMode buildMode = bmNormal) override
    {
        unsupported("buildDerivation");
    }

    void addTempRoot(const StorePath & path) override {}

    void addIndirectRoot(const Path & path) override {}

    Roots findRoots(bool censor) override
    {
        return Roots();
    }

    void collectGarbage(const GCOptions & options, GCResults & results) override {}

    void addSignatures(const StorePath & storePath, const StringSet & sigs) override
    {
        unsupported("addSignatures");
    }

    void queryMissing(
        const std::vector<DerivedPath> & targets,
        StorePathSet & willBuild,
        StorePathSet & willSubstitute,
        StorePathSet & unknown,
        uint64_t & downloadSize,
        uint64_t & narSize) override;

    virtual std::optional<std::string> getBuildLogExact(const StorePath & path) override
    {
        return std::nullopt;
    }

    virtual void addBuildLog(const StorePath & path, std::string_view log) override
    {
        unsupported("addBuildLog");
    }

    std::optional<TrustedFlag> isTrustedClient() override
    {
        return NotTrusted;
    }
};

}
