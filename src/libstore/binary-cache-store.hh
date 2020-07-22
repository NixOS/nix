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

    virtual bool fileExists(const std::string & path) = 0;

    virtual void upsertFile(const std::string & path,
        std::shared_ptr<std::basic_iostream<char>> istream,
        const std::string & mimeType) = 0;

    void upsertFile(const std::string & path,
        std::string && data,
        const std::string & mimeType);

    /* Note: subclasses must implement at least one of the two
       following getFile() methods. */

    /* Dump the contents of the specified file to a sink. */
    virtual void getFile(const std::string & path, Sink & sink);

    /* Fetch the specified file and call the specified callback with
       the result. A subclass may implement this asynchronously. */
    virtual void getFile(const std::string & path,
        Callback<std::shared_ptr<std::string>> callback) noexcept;

    std::shared_ptr<std::string> getFile(const std::string & path);

public:

    virtual void init();

private:

    std::string narMagic;

    std::string narInfoFileFor(const StorePath & storePath);

    void writeNarInfo(ref<NarInfo> narInfo);

public:

    bool isValidPathUncached(StorePathOrDesc path) override;

    void queryPathInfoUncached(StorePathOrDesc path,
        Callback<std::shared_ptr<const ValidPathInfo>> callback) noexcept override;

    std::optional<StorePath> queryPathFromHashPart(const std::string & hashPart) override
    { unsupported("queryPathFromHashPart"); }

    void addToStore(const ValidPathInfo & info, Source & narSource,
        RepairFlag repair, CheckSigsFlag checkSigs) override;

    StorePath addToStore(const string & name, const Path & srcPath,
        FileIngestionMethod method, HashType hashAlgo,
        PathFilter & filter, RepairFlag repair) override;

    StorePath addTextToStore(const string & name, const string & s,
        const StorePathSet & references, RepairFlag repair) override;

    void narFromPath(StorePathOrDesc path, Sink & sink) override;

    BuildResult buildDerivation(const StorePath & drvPath, const BasicDerivation & drv,
        BuildMode buildMode) override
    { unsupported("buildDerivation"); }

    void ensurePath(StorePathOrDesc path) override
    { unsupported("ensurePath"); }

    ref<FSAccessor> getFSAccessor() override;

    void addSignatures(const StorePath & storePath, const StringSet & sigs) override;

    std::shared_ptr<std::string> getBuildLog(const StorePath & path) override;

};

MakeError(NoSuchBinaryCacheFile, Error);

}
