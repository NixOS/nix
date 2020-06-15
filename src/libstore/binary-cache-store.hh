#pragma once

#include "crypto.hh"
#include "store-api.hh"

#include "pool.hh"

#include <atomic>

namespace nix {

struct NarInfo;

class BinaryCacheStore : public Store
{
public:

    const Setting<std::string> compression{this, "xz", "compression", "NAR compression method ('xz', 'bzip2', or 'none')"};
    const Setting<bool> writeNARListing{this, false, "write-nar-listing", "whether to write a JSON file listing the files in each NAR"};
    const Setting<bool> writeDebugInfo{this, false, "index-debug-info", "whether to index DWARF debug info files by build ID"};
    const Setting<Path> secretKeyFile{this, "", "secret-key", "path to secret key used to sign the binary cache"};
    const Setting<Path> localNarCache{this, "", "local-nar-cache", "path to a local cache of NARs"};
    const Setting<bool> parallelCompression{this, false, "parallel-compression",
        "enable multi-threading compression, available for xz only currently"};

private:

    std::unique_ptr<SecretKey> secretKey;

protected:

    BinaryCacheStore(const Params & params);

public:

    virtual bool fileExists(std::string_view path) = 0;

    virtual void upsertFile(std::string_view path,
        std::string_view data,
        std::string_view mimeType) = 0;

    /* Note: subclasses must implement at least one of the two
       following getFile() methods. */

    /* Dump the contents of the specified file to a sink. */
    virtual void getFile(std::string_view path, Sink & sink);

    /* Fetch the specified file and call the specified callback with
       the result. A subclass may implement this asynchronously. */
    virtual void getFile(std::string_view path,
        Callback<std::shared_ptr<std::string>> callback) noexcept;

    std::shared_ptr<std::string> getFile(std::string_view path);

public:

    virtual void init();

private:

    std::string narMagic;

    std::string narInfoFileFor(const StorePath & storePath);

    void writeNarInfo(ref<NarInfo> narInfo);

public:

    bool isValidPathUncached(const StorePath & path) override;

    void queryPathInfoUncached(const StorePath & path,
        Callback<std::shared_ptr<const ValidPathInfo>> callback) noexcept override;

    std::optional<StorePath> queryPathFromHashPart(std::string_view hashPart) override
    { unsupported("queryPathFromHashPart"); }

    void addToStore(const ValidPathInfo & info, Source & narSource,
        RepairFlag repair, CheckSigsFlag checkSigs,
        std::shared_ptr<FSAccessor> accessor) override;

    StorePath addToStore(std::string_view name, PathView srcPath,
        FileIngestionMethod method, HashType hashAlgo,
        PathFilter & filter, RepairFlag repair) override;

    StorePath addTextToStore(std::string_view name, std::string_view s,
        const StorePathSet & references, RepairFlag repair) override;

    void narFromPath(const StorePath & path, Sink & sink) override;

    BuildResult buildDerivation(const StorePath & drvPath, const BasicDerivation & drv,
        BuildMode buildMode) override
    { unsupported("buildDerivation"); }

    void ensurePath(const StorePath & path) override
    { unsupported("ensurePath"); }

    ref<FSAccessor> getFSAccessor() override;

    void addSignatures(const StorePath & storePath, const StringSet & sigs) override;

    std::shared_ptr<std::string> getBuildLog(const StorePath & path) override;

};

MakeError(NoSuchBinaryCacheFile, Error);

}
