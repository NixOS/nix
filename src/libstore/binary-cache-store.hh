#pragma once
///@file

#include "signature/local-keys.hh"
#include "store-api.hh"
#include "log-store.hh"

#include "pool.hh"

#include <atomic>

namespace nix {

struct NarInfo;

template<template<typename> class F>
struct BinaryCacheStoreConfigT
{
    F<std::string> compression;
    F<bool> writeNARListing;
    F<bool> writeDebugInfo;
    F<Path> secretKeyFile;
    F<Path> localNarCache;
    F<bool> parallelCompression;
    F<int> compressionLevel;
};

struct BinaryCacheStoreConfig :
    BinaryCacheStoreConfigT<config::JustValue>
{
    static config::SettingDescriptionMap descriptions();

    const Store::Config & storeConfig;

    BinaryCacheStoreConfig(const Store::Config &, const StoreReference::Params &);
};

/**
 * @note subclasses must implement at least one of the two
 * virtual getFile() methods.
 */
struct BinaryCacheStore :
    virtual Store,
    virtual LogStore
{
    using Config = BinaryCacheStoreConfig;

    const Config & config;

private:
    std::unique_ptr<Signer> signer;

protected:

    // The prefix under which realisation infos will be stored
    const std::string realisationsPrefix = "realisations";

    BinaryCacheStore(const Config &);

public:

    virtual bool fileExists(const std::string & path) = 0;

    virtual void upsertFile(const std::string & path,
        std::shared_ptr<std::basic_iostream<char>> istream,
        const std::string & mimeType) = 0;

    void upsertFile(const std::string & path,
        // FIXME: use std::string_view
        std::string && data,
        const std::string & mimeType);

    /**
     * Dump the contents of the specified file to a sink.
     */
    virtual void getFile(const std::string & path, Sink & sink);

    /**
     * Fetch the specified file and call the specified callback with
     * the result. A subclass may implement this asynchronously.
     */
    virtual void getFile(
        const std::string & path,
        Callback<std::optional<std::string>> callback) noexcept;

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
        Source & narSource, RepairFlag repair, CheckSigsFlag checkSigs,
        std::function<ValidPathInfo(HashResult)> mkInfo);

public:

    bool isValidPathUncached(const StorePath & path) override;

    void queryPathInfoUncached(const StorePath & path,
        Callback<std::shared_ptr<const ValidPathInfo>> callback) noexcept override;

    std::optional<StorePath> queryPathFromHashPart(const std::string & hashPart) override;

    void addToStore(const ValidPathInfo & info, Source & narSource,
        RepairFlag repair, CheckSigsFlag checkSigs) override;

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

    void queryRealisationUncached(const DrvOutput &,
        Callback<std::shared_ptr<const Realisation>> callback) noexcept override;

    void narFromPath(const StorePath & path, Sink & sink) override;

    ref<SourceAccessor> getFSAccessor(bool requireValidPath = true) override;

    void addSignatures(const StorePath & storePath, const StringSet & sigs) override;

    std::optional<std::string> getBuildLogExact(const StorePath & path) override;

    void addBuildLog(const StorePath & drvPath, std::string_view log) override;

};

MakeError(NoSuchBinaryCacheFile, Error);

}
