#pragma once

#include "crypto.hh"
#include "store-api.hh"
#include "log-store.hh"

#include "pool.hh"

#include <atomic>

namespace nix {

struct NarInfo;

struct BinaryCacheStoreConfig : virtual StoreConfig
{
    using StoreConfig::StoreConfig;

    const Setting<std::string> compression{(StoreConfig*) this, "xz", "compression", "NAR compression method ('xz', 'bzip2', 'gzip', 'zstd', or 'none')"};
    const Setting<bool> writeNARListing{(StoreConfig*) this, false, "write-nar-listing", "whether to write a JSON file listing the files in each NAR"};
    const Setting<bool> writeDebugInfo{(StoreConfig*) this, false, "index-debug-info", "whether to index DWARF debug info files by build ID"};
    const Setting<Path> secretKeyFile{(StoreConfig*) this, "", "secret-key", "path to secret key used to sign the binary cache"};
    const Setting<Path> localNarCache{(StoreConfig*) this, "", "local-nar-cache", "path to a local cache of NARs"};
    const Setting<bool> parallelCompression{(StoreConfig*) this, false, "parallel-compression",
        "enable multi-threading compression for NARs, available for xz and zstd only currently"};
    const Setting<int> compressionLevel{(StoreConfig*) this, -1, "compression-level",
        "specify 'preset level' of compression to be used with NARs: "
        "meaning and accepted range of values depends on compression method selected, "
        "other than -1 which we reserve to indicate Nix defaults should be used"};
};

class BinaryCacheStore : public virtual BinaryCacheStoreConfig,
    public virtual Store,
    public virtual LogStore
{

private:

    std::unique_ptr<SecretKey> secretKey;

protected:

    // The prefix under which realisation infos will be stored
    const std::string realisationsPrefix = "realisations";

    BinaryCacheStore(const Params & params);

public:

    virtual bool fileExists(const std::string & path) = 0;

    virtual void upsertFile(const std::string & path,
        std::shared_ptr<std::basic_iostream<char>> istream,
        const std::string & mimeType) = 0;

    void upsertFile(const std::string & path,
        // FIXME: use std::string_view
        std::string && data,
        const std::string & mimeType);

    /* Note: subclasses must implement at least one of the two
       following getFile() methods. */

    /* Dump the contents of the specified file to a sink. */
    virtual void getFile(const std::string & path, Sink & sink);

    /* Fetch the specified file and call the specified callback with
       the result. A subclass may implement this asynchronously. */
    virtual void getFile(
        const std::string & path,
        Callback<std::optional<std::string>> callback) noexcept;

    std::optional<std::string> getFile(const std::string & path);

public:

    virtual void init() override;

private:

    std::string narMagic;

    std::string narInfoFileFor(const StorePath & storePath);

    void writeNarInfo(ref<NarInfo> narInfo);

    ref<const ValidPathInfo> addToStoreCommon(
        Source & narSource, RepairFlag repair, CheckSigsFlag checkSigs,
        std::function<ValidPathInfo(HashResult)> mkInfo);

public:

    bool isValidPathUncached(StorePathOrDesc path) override;

    void queryPathInfoUncached(StorePathOrDesc path,
        Callback<std::shared_ptr<const ValidPathInfo>> callback) noexcept override;

    std::optional<StorePath> queryPathFromHashPart(const std::string & hashPart) override
    { unsupported("queryPathFromHashPart"); }

    void addToStore(const ValidPathInfo & info, Source & narSource,
        RepairFlag repair, CheckSigsFlag checkSigs) override;

    StorePath addToStoreFromDump(Source & dump, std::string_view name,
        FileIngestionMethod method, HashType hashAlgo, RepairFlag repair, const StorePathSet & references) override;

    StorePath addToStore(
        std::string_view name,
        const Path & srcPath,
        FileIngestionMethod method,
        HashType hashAlgo,
        PathFilter & filter,
        RepairFlag repair,
        const StorePathSet & references) override;

    StorePath addTextToStore(
        std::string_view name,
        std::string_view s,
        const StorePathSet & references,
        RepairFlag repair) override;

    void registerDrvOutput(const Realisation & info) override;

    void queryRealisationUncached(const DrvOutput &,
        Callback<std::shared_ptr<const Realisation>> callback) noexcept override;

    void narFromPath(StorePathOrDesc path, Sink & sink) override;

    ref<FSAccessor> getFSAccessor() override;

    void addSignatures(const StorePath & storePath, const StringSet & sigs) override;

    std::optional<std::string> getBuildLog(const StorePath & path) override;

    void addBuildLog(const StorePath & drvPath, std::string_view log) override;

};

MakeError(NoSuchBinaryCacheFile, Error);

}
