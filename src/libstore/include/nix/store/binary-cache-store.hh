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

struct BinaryCacheStoreConfig : virtual StoreConfig
{
    using StoreConfig::StoreConfig;

    const Setting<std::string> compression{
        this, "xz", "compression", "NAR compression method (`xz`, `bzip2`, `gzip`, `zstd`, or `none`)."};

    const Setting<bool> writeNARListing{
        this, false, "write-nar-listing", "Whether to write a JSON file that lists the files in each NAR."};

    const Setting<bool> writeDebugInfo{
        this,
        false,
        "index-debug-info",
        R"(
          Whether to index DWARF debug info files by build ID. This allows [`dwarffs`](https://github.com/edolstra/dwarffs) to
          fetch debug info on demand
        )"};

    const Setting<Path> secretKeyFile{this, "", "secret-key", "Path to the secret key used to sign the binary cache."};

    const Setting<std::string> secretKeyFiles{
        this, "", "secret-keys", "List of comma-separated paths to the secret keys used to sign the binary cache."};

    const Setting<Path> localNarCache{
        this,
        "",
        "local-nar-cache",
        "Path to a local cache of NARs fetched from this binary cache, used by commands such as `nix store cat`."};

    const Setting<bool> parallelCompression{
        this,
        false,
        "parallel-compression",
        "Enable multi-threaded compression of NARs. This is currently only available for `xz` and `zstd`."};

    const Setting<int> compressionLevel{
        this,
        -1,
        "compression-level",
        R"(
          The *preset level* to be used when compressing NARs.
          The meaning and accepted values depend on the compression method selected.
          `-1` specifies that the default compression level should be used.
        )"};
};

/**
 * @note subclasses must implement at least one of the two
 * virtual getFile() methods.
 */
struct BinaryCacheStore : virtual Store, virtual LogStore
{
    using Config = BinaryCacheStoreConfig;

    /**
     * Intentionally mutable because some things we update due to the
     * cache's own (remote side) settings.
     */
    Config & config;

private:
    std::vector<std::unique_ptr<Signer>> signers;

protected:

    /**
     * The prefix under which realisation infos will be stored
     */
    constexpr const static std::string realisationsPrefix = "realisations";

    constexpr const static std::string cacheInfoFile = "nix-cache-info";

    BinaryCacheStore(Config &);

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

    virtual void init() override;

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
