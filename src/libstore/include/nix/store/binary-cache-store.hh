#pragma once
///@file

#include "nix/util/signature/local-keys.hh"
#include "nix/store/store-api.hh"
#include "nix/store/log-store.hh"

#include "nix/util/pool.hh"

#include <atomic>

namespace nix {

struct NarInfo;
class RemoteFSAccessor;

template<template<typename> class F>
struct BinaryCacheStoreConfigT
{
    F<std::string>::type compression;
    F<bool>::type writeNARListing;
    F<bool>::type writeDebugInfo;
    F<Path>::type secretKeyFile;
    F<std::vector<Path>>::type secretKeyFiles;
    F<Path>::type localNarCache;
    F<bool>::type parallelCompression;
    F<int>::type compressionLevel;
};

struct BinaryCacheStoreConfig : BinaryCacheStoreConfigT<config::PlainValue>
{
    static config::SettingDescriptionMap descriptions();

    const Store::Config & storeConfig;

    BinaryCacheStoreConfig(const Store::Config &, const StoreConfig::Params &);
};

/**
 * @note subclasses must implement at least one of the two
 * virtual getFile() methods.
 */
struct BinaryCacheStore : virtual Store, virtual LogStore
{
    using Config = BinaryCacheStoreConfig;

    const Config & config;

private:
    std::vector<std::unique_ptr<Signer>> signers;

protected:

    /**
     * The prefix under which realisation infos will be stored
     */
    constexpr const static std::string realisationsPrefix = "realisations";

    constexpr const static std::string cacheInfoFile = "nix-cache-info";

    BinaryCacheStore(const Config &);

    /**
     * Compute the path to the given realisation
     *
     * It's `${realisationsPrefix}/${drvOutput}.doi`.
     */
    std::string makeRealisationPath(const DrvOutput & id);

public:

    virtual bool fileExists(const std::string & path) = 0;

    virtual void upsertFile(
        const std::string & path, RestartableSource & source, const std::string & mimeType, uint64_t sizeHint) = 0;

    void upsertFile(
        const std::string & path,
        // FIXME: use std::string_view
        std::string && data,
        const std::string & mimeType,
        uint64_t sizeHint);

    void upsertFile(
        const std::string & path,
        // FIXME: use std::string_view
        std::string && data,
        const std::string & mimeType)
    {
        auto size = data.size();
        upsertFile(path, std::move(data), mimeType, size);
    }

    /**
     * Dump the contents of the specified file to a sink.
     */
    virtual void getFile(const std::string & path, Sink & sink);

    /**
     * Get the contents of /nix-cache-info. Return std::nullopt if it
     * doesn't exist.
     */
    virtual std::optional<std::string> getNixCacheInfo();

    /**
     * Fetch the specified file and call the specified callback with
     * the result. A subclass may implement this asynchronously.
     */
    virtual void getFile(const std::string & path, Callback<std::optional<std::string>> callback) noexcept;

    std::optional<std::string> getFile(const std::string & path);

public:

    /**
     * Perform any necessary effectful operation to make the store up and
     * running
     */
    virtual void init();

private:

    std::string narMagic;

    std::string narInfoFileFor(const StorePath & storePath);

    void writeNarInfo(ref<NarInfo> narInfo);

    ref<const ValidPathInfo> addToStoreCommon(
        Source & narSource,
        RepairFlag repair,
        CheckSigsFlag checkSigs,
        std::function<ValidPathInfo(HashResult)> mkInfo);

    /**
     * Same as `getFSAccessor`, but with a more preceise return type.
     */
    ref<RemoteFSAccessor> getRemoteFSAccessor(bool requireValidPath = true);

public:

    bool isValidPathUncached(const StorePath & path) override;

    void queryPathInfoUncached(
        const StorePath & path, Callback<std::shared_ptr<const ValidPathInfo>> callback) noexcept override;

    std::optional<StorePath> queryPathFromHashPart(const std::string & hashPart) override;

    void
    addToStore(const ValidPathInfo & info, Source & narSource, RepairFlag repair, CheckSigsFlag checkSigs) override;

    StorePath addToStoreFromDump(
        Source & dump,
        std::string_view name,
        FileSerialisationMethod dumpMethod,
        ContentAddressMethod hashMethod,
        HashAlgorithm hashAlgo,
        const StorePathSet & references,
        RepairFlag repair) override;

    StorePath addToStore(
        std::string_view name,
        const SourcePath & path,
        ContentAddressMethod method,
        HashAlgorithm hashAlgo,
        const StorePathSet & references,
        PathFilter & filter,
        RepairFlag repair) override;

    void registerDrvOutput(const Realisation & info) override;

    void queryRealisationUncached(
        const DrvOutput &, Callback<std::shared_ptr<const UnkeyedRealisation>> callback) noexcept override;

    void narFromPath(const StorePath & path, Sink & sink) override;

    ref<SourceAccessor> getFSAccessor(bool requireValidPath = true) override;

    std::shared_ptr<SourceAccessor> getFSAccessor(const StorePath &, bool requireValidPath = true) override;

    void addSignatures(const StorePath & storePath, const StringSet & sigs) override;

    std::optional<std::string> getBuildLogExact(const StorePath & path) override;

    void addBuildLog(const StorePath & drvPath, std::string_view log) override;
};

MakeError(NoSuchBinaryCacheFile, Error);

} // namespace nix
