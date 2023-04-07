#pragma once
///@file

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

    const Setting<std::string> compression{(StoreConfig*) this, "xz", "compression",
        "NAR compression method (`xz`, `bzip2`, `gzip`, `zstd`, or `none`)."};

    const Setting<bool> writeNARListing{(StoreConfig*) this, false, "write-nar-listing",
        "Whether to write a JSON file that lists the files in each NAR."};

    const Setting<bool> writeDebugInfo{(StoreConfig*) this, false, "index-debug-info",
        R"(
          Whether to index DWARF debug info files by build ID. This allows [`dwarffs`](https://github.com/edolstra/dwarffs) to
          fetch debug info on demand
        )"};

    const Setting<Path> secretKeyFile{(StoreConfig*) this, "", "secret-key",
        "Path to the secret key used to sign the binary cache."};

    const Setting<Path> localNarCache{(StoreConfig*) this, "", "local-nar-cache",
        "Path to a local cache of NARs fetched from this binary cache, used by commands such as `nix store cat`."};

    const Setting<bool> parallelCompression{(StoreConfig*) this, false, "parallel-compression",
        "Enable multi-threaded compression of NARs. This is currently only available for `xz` and `zstd`."};

    const Setting<int> compressionLevel{(StoreConfig*) this, -1, "compression-level",
        R"(
          The *preset level* to be used when compressing NARs.
          The meaning and accepted values depend on the compression method selected.
          `-1` specifies that the default compression level should be used.
        )"};
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

    bool isValidPathUncached(const StorePath & path) override;

    void queryPathInfoUncached(const StorePath & path,
        Callback<std::shared_ptr<const ValidPathInfo>> callback) noexcept override;

    std::optional<StorePath> queryPathFromHashPart(const std::string & hashPart) override;

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

    void narFromPath(const StorePath & path, Sink & sink) override;

    ref<FSAccessor> getFSAccessor() override;

    void addSignatures(const StorePath & storePath, const StringSet & sigs) override;

    std::optional<std::string> getBuildLogExact(const StorePath & path) override;

    void addBuildLog(const StorePath & drvPath, std::string_view log) override;

};

MakeError(NoSuchBinaryCacheFile, Error);

}
