#pragma once

#include "binary-cache-store.hh"

namespace nix {

MakeError(UploadToIPFS, Error);

class IPFSBinaryCacheStore : public Store
{

public:

    const Setting<std::string> compression{this, "xz", "compression", "NAR compression method ('xz', 'bzip2', or 'none')"};
    const Setting<Path> secretKeyFile{this, "", "secret-key", "path to secret key used to sign the binary cache"};
    const Setting<bool> parallelCompression{this, false, "parallel-compression",
        "enable multi-threading compression, available for xz only currently"};

    // FIXME: merge with allowModify bool
    const Setting<bool> _allowModify{this, false, "allow-modify",
        "allow Nix to update IPFS/IPNS address when appropriate"};

private:

    bool allowModify;

    std::unique_ptr<SecretKey> secretKey;
    std::string narMagic;

    std::string cacheUri;
    std::string daemonUri;

    std::string getIpfsPath() {
        auto state(_state.lock());
        return state->ipfsPath;
    }
    std::string initialIpfsPath;
    std::optional<string> ipnsPath;

    struct State
    {
        std::string ipfsPath;
    };
    Sync<State> _state;

public:

    IPFSBinaryCacheStore(const Params & params, const Path & _cacheUri);

    std::string getUri() override
    {
        return cacheUri;
    }

private:

    std::string putIpfsDag(nlohmann::json data);

    nlohmann::json getIpfsDag(std::string objectPath);

    // Given a ipns path, checks if it corresponds to a DNSLink path, and in
    // case returns the domain
    static std::optional<string> isDNSLinkPath(std::string path);

    bool ipfsObjectExists(const std::string ipfsPath);

    bool fileExists(const std::string & path)
    {
        return ipfsObjectExists(getIpfsPath() + "/" + path);
    }

    // Resolve the IPNS name to an IPFS object
    std::string resolveIPNSName(std::string ipnsPath);

public:
    Path formatPathAsProtocol(Path path);

    // IPNS publish can be slow, we try to do it rarely.
    void sync() override;

private:

    void addLink(std::string name, std::string ipfsObject);

    std::string addFile(const std::string & data);

    void upsertFile(const std::string & path, const std::string & data, const std::string & mimeType);

    void getFile(const std::string & path,
        Callback<std::shared_ptr<std::string>> callback) noexcept;

    void getFile(const std::string & path, Sink & sink);

    std::shared_ptr<std::string> getFile(const std::string & path);

    void getIpfsObject(const std::string & ipfsPath,
        Callback<std::shared_ptr<std::string>> callback) noexcept;

    void writeNarInfo(ref<NarInfo> narInfo);

public:

    void addToStore(const ValidPathInfo & info, Source & narSource,
        RepairFlag repair, CheckSigsFlag checkSigs) override;

    bool isValidPathUncached(const StorePath & storePath) override;

    void narFromPath(const StorePath & storePath, Sink & sink) override;

    void queryPathInfoUncached(const StorePath & storePath,
        Callback<std::shared_ptr<const ValidPathInfo>> callback) noexcept override;

    StorePath addToStore(const string & name, const Path & srcPath,
        FileIngestionMethod method, HashType hashAlgo, PathFilter & filter, RepairFlag repair) override;

    StorePath addTextToStore(const string & name, const string & s,
        const StorePathSet & references, RepairFlag repair) override;

    void addSignatures(const StorePath & storePath, const StringSet & sigs) override;

    virtual void addTempRoot(const StorePath & path) override;

    std::shared_ptr<std::string> getBuildLog(const StorePath & path) override
    { unsupported("getBuildLog"); }

    BuildResult buildDerivation(const StorePath & drvPath, const BasicDerivation & drv,
        BuildMode buildMode) override
    { unsupported("buildDerivation"); }

    void ensurePath(const StorePath & path) override
    { unsupported("ensurePath"); }

    std::optional<StorePath> queryPathFromHashPart(const std::string & hashPart) override
    { unsupported("queryPathFromHashPart"); }

};

}
