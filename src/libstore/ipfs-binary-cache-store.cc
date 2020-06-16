#include <cstring>
#include <nlohmann/json.hpp>

#include "binary-cache-store.hh"
#include "filetransfer.hh"
#include "nar-info-disk-cache.hh"
#include "archive.hh"
#include "compression.hh"

namespace nix {

MakeError(UploadToIPFS, Error);

class IPFSBinaryCacheStore : public Store
{

public:

    const Setting<std::string> compression{this, "xz", "compression", "NAR compression method ('xz', 'bzip2', or 'none')"};
    const Setting<Path> secretKeyFile{this, "", "secret-key", "path to secret key used to sign the binary cache"};
    const Setting<bool> parallelCompression{this, false, "parallel-compression",
        "enable multi-threading compression, available for xz only currently"};

private:

    std::unique_ptr<SecretKey> secretKey;
    std::string narMagic;

    std::string cacheUri;
    std::string daemonUri;

    std::string getIpfsPath() {
        auto state(_state.lock());
        return state->ipfsPath;
    }
    std::string initialIpfsPath;
    std::optional<string> optIpnsPath;

    struct State
    {
        std::string ipfsPath;
    };
    Sync<State> _state;

public:

    IPFSBinaryCacheStore(
      const Params & params, const Path & _cacheUri)
            : Store(params)
            , cacheUri(_cacheUri)
    {
        auto state(_state.lock());

        if (secretKeyFile != "")
            secretKey = std::unique_ptr<SecretKey>(new SecretKey(readFile(secretKeyFile)));

        StringSink sink;
        sink << narVersionMagic1;
        narMagic = *sink.s;

        if (cacheUri.back() == '/')
            cacheUri.pop_back();

        if (hasPrefix(cacheUri, "ipfs://")) {
            initialIpfsPath = "/ipfs/" + std::string(cacheUri, 7);
            state->ipfsPath = initialIpfsPath;
        } else if (hasPrefix(cacheUri, "ipns://"))
            optIpnsPath = "/ipns/" + std::string(cacheUri, 7);
        else
            throw Error("unknown IPNS URI '%s'", cacheUri);

        std::string ipfsAPIHost(get(params, "host").value_or("127.0.0.1"));
        std::string ipfsAPIPort(get(params, "port").value_or("5001"));
        daemonUri = "http://" + ipfsAPIHost + ":" + ipfsAPIPort;

        // Check the IPFS daemon is running
        FileTransferRequest request(daemonUri + "/api/v0/version");
        request.post = true;
        request.tries = 1;
        auto res = getFileTransfer()->download(request);
        auto versionInfo = nlohmann::json::parse(*res.data);
        if (versionInfo.find("Version") == versionInfo.end())
            throw Error("daemon for IPFS is not running properly");

        // Resolve the IPNS name to an IPFS object
        if (optIpnsPath) {
            auto ipnsPath = *optIpnsPath;
            initialIpfsPath = resolveIPNSName(ipnsPath, true);
            state->ipfsPath = initialIpfsPath;
        }
    }

    std::string getUri() override
    {
        return cacheUri;
    }

private:

    std::string putIpfsDag(std::string data)
    {
        auto req = FileTransferRequest(daemonUri + "/api/v0/dag/put");
        req.data = std::make_shared<string>(data);
        req.post = true;
        req.tries = 1;
        auto res = getFileTransfer()->upload(req);
        auto json = nlohmann::json::parse(*res.data);
        return json["Cid"]["/"];
    }

    std::string getIpfsDag(std::string objectPath)
    {
        auto req = FileTransferRequest(daemonUri + "/api/v0/dag/get?arg=" + objectPath);
        req.post = true;
        req.tries = 1;
        auto res = getFileTransfer()->download(req);
        return *res.data;
    }

    // Given a ipns path, checks if it corresponds to a DNSLink path, and in
    // case returns the domain
    static std::optional<string> isDNSLinkPath(std::string path)
    {
        if (path.find("/ipns/") != 0)
            throw Error("The provided path is not a ipns path");
        auto subpath = std::string(path, 6);
        if (subpath.find(".") != std::string::npos) {
            return subpath;
        }
        return std::nullopt;
    }

    bool ipfsObjectExists(const std::string ipfsPath)
    {
        auto uri = daemonUri + "/api/v0/object/stat?arg=" + getFileTransfer()->urlEncode(ipfsPath);

        FileTransferRequest request(uri);
        request.post = true;
        request.tries = 1;
        try {
            auto res = getFileTransfer()->download(request);
            auto json = nlohmann::json::parse(*res.data);

            return json.find("Hash") != json.end();
        } catch (FileTransferError & e) {
            // probably should verify this is a not found error but
            // ipfs gives us a 500
            return false;
        }
    }

protected:

    bool fileExists(const std::string & path)
    {
        return ipfsObjectExists(getIpfsPath() + "/" + path);
    }

private:

    // Resolve the IPNS name to an IPFS object
    std::string resolveIPNSName(std::string ipnsPath, bool offline) {
        debug("Resolving IPFS object of '%s', this could take a while.", ipnsPath);
        auto uri = daemonUri + "/api/v0/name/resolve?offline=" + (offline?"true":"false") + "&arg=" + getFileTransfer()->urlEncode(ipnsPath);
        FileTransferRequest request(uri);
        request.post = true;
        request.tries = 1;
        auto res = getFileTransfer()->download(request);
        auto json = nlohmann::json::parse(*res.data);
        if (json.find("Path") == json.end())
            throw Error("daemon for IPFS is not running properly");
        return json["Path"];
    }

public:

    // IPNS publish can be slow, we try to do it rarely.
    void sync() override
    {
        auto state(_state.lock());

        if (!optIpnsPath) {
            throw Error("We don't have an ipns path and the current ipfs address doesn't match the initial one.\n  current: %s\n  initial: %s",
                state->ipfsPath, initialIpfsPath);
        }

        auto ipnsPath = *optIpnsPath;

        auto resolvedIpfsPath = resolveIPNSName(ipnsPath, false);
        if (resolvedIpfsPath != initialIpfsPath) {
            throw Error("The IPNS hash or DNS link %s resolves now to something different from the value it had when Nix was started;\n  wanted: %s\n  got %s\nPerhaps something else updated it in the meantime?",
                initialIpfsPath, resolvedIpfsPath);
        }

        if (resolvedIpfsPath == state->ipfsPath) {
            printMsg(lvlInfo, "The hash is already up to date, nothing to do");
            return;
        }

        // Now, we know that paths are not up to date but also not changed due to updates in DNS or IPNS hash.
        auto optDomain = isDNSLinkPath(ipnsPath);
        if (optDomain) {
            auto domain = *optDomain;
            throw Error("The provided ipns path is a DNSLink, and syncing those is not supported.\n  Current DNSLink: %s\nYou should update your DNS settings"
                , domain);
        }

        debug("Publishing '%s' to '%s', this could take a while.", state->ipfsPath, ipnsPath);

        auto uri = daemonUri + "/api/v0/name/publish?offline=true&arg=" + getFileTransfer()->urlEncode(state->ipfsPath);
        uri += "&key=" + std::string(ipnsPath, 6);

        auto req = FileTransferRequest(uri);
        req.post = true;
        req.tries = 1;
        getFileTransfer()->download(req);
    }

private:

    void addLink(std::string name, std::string ipfsObject)
    {
        auto state(_state.lock());

        auto uri = daemonUri + "/api/v0/object/patch/add-link?create=true";
        uri += "&arg=" + getFileTransfer()->urlEncode(state->ipfsPath);
        uri += "&arg=" + getFileTransfer()->urlEncode(name);
        uri += "&arg=" + getFileTransfer()->urlEncode(ipfsObject);

        auto req = FileTransferRequest(uri);
        req.post = true;
        req.tries = 1;
        auto res = getFileTransfer()->download(req);
        auto json = nlohmann::json::parse(*res.data);

        state->ipfsPath = "/ipfs/" + (std::string) json["Hash"];
    }

    std::string addFile(const std::string & data)
    {
        // TODO: use callbacks

        auto req = FileTransferRequest(daemonUri + "/api/v0/add");
        req.data = std::make_shared<string>(data);
        req.post = true;
        req.tries = 1;
        auto res = getFileTransfer()->upload(req);
        auto json = nlohmann::json::parse(*res.data);
        return (std::string) json["Hash"];
    }

    void upsertFile(const std::string & path, const std::string & data, const std::string & mimeType)
    {
        try {
            addLink(path, "/ipfs/" + addFile(data));
        } catch (FileTransferError & e) {
            throw UploadToIPFS("while uploading to IPFS binary cache at '%s': %s", cacheUri, e.msg());
        }
    }

    void getFile(const std::string & path,
        Callback<std::shared_ptr<std::string>> callback) noexcept
    {
        getIpfsObject(getIpfsPath() + "/" + path, std::move(callback));
    }

    void getFile(const std::string & path, Sink & sink)
    {
        std::promise<std::shared_ptr<std::string>> promise;
        getFile(path,
            {[&](std::future<std::shared_ptr<std::string>> result) {
                try {
                    promise.set_value(result.get());
                } catch (...) {
                    promise.set_exception(std::current_exception());
                }
            }});
        auto data = promise.get_future().get();
        sink((unsigned char *) data->data(), data->size());
    }

    std::shared_ptr<std::string> getFile(const std::string & path)
    {
        StringSink sink;
        try {
            getFile(path, sink);
        } catch (NoSuchBinaryCacheFile &) {
            return nullptr;
        }
        return sink.s;
    }

    void getIpfsObject(const std::string & ipfsPath,
        Callback<std::shared_ptr<std::string>> callback) noexcept
    {
        auto uri = daemonUri + "/api/v0/cat?arg=" + getFileTransfer()->urlEncode(ipfsPath);

        FileTransferRequest request(uri);
        request.post = true;
        request.tries = 1;

        auto callbackPtr = std::make_shared<decltype(callback)>(std::move(callback));

        getFileTransfer()->enqueueFileTransfer(request,
            {[callbackPtr](std::future<FileTransferResult> result){
                try {
                    (*callbackPtr)(result.get().data);
                } catch (FileTransferError & e) {
                    return (*callbackPtr)(std::shared_ptr<std::string>());
                } catch (...) {
                    callbackPtr->rethrow();
                }
            }}
        );
    }

    std::string narInfoFileFor(const StorePath & storePath)
    {
        return storePathToHash(printStorePath(storePath)) + ".narinfo";
    }

    void writeNarInfo(ref<NarInfo> narInfo)
    {
        auto narInfoFile = narInfoFileFor(narInfo->path);

        upsertFile(narInfoFile, narInfo->to_string(*this), "text/x-nix-narinfo");

        auto hashPart = storePathToHash(printStorePath(narInfo->path));

        {
            auto state_(state.lock());
            state_->pathInfoCache.upsert(hashPart, PathInfoCacheValue { .value = std::shared_ptr<NarInfo>(narInfo) });
        }
    }

public:

    void addToStore(const ValidPathInfo & info, Source & narSource,
        RepairFlag repair, CheckSigsFlag checkSigs, std::shared_ptr<FSAccessor> accessor) override
    {
        // FIXME: See if we can use the original source to reduce memory usage.
        auto nar = make_ref<std::string>(narSource.drain());

        if (!repair && isValidPath(info.path)) return;

        /* Verify that all references are valid. This may do some .narinfo
           reads, but typically they'll already be cached. */
        for (auto & ref : info.references)
            try {
                if (ref != info.path)
                    queryPathInfo(ref);
            } catch (InvalidPath &) {
                throw Error("cannot add '%s' to the binary cache because the reference '%s' is not valid",
                    printStorePath(info.path), printStorePath(ref));
            }

        assert(nar->compare(0, narMagic.size(), narMagic) == 0);

        auto narInfo = make_ref<NarInfo>(info);

        narInfo->narSize = nar->size();
        narInfo->narHash = hashString(htSHA256, *nar);

        if (info.narHash && info.narHash != narInfo->narHash)
            throw Error("refusing to copy corrupted path '%1%' to binary cache", printStorePath(info.path));

        /* Compress the NAR. */
        narInfo->compression = compression;
        auto now1 = std::chrono::steady_clock::now();
        auto narCompressed = compress(compression, *nar, parallelCompression);
        auto now2 = std::chrono::steady_clock::now();
        narInfo->fileHash = hashString(htSHA256, *narCompressed);
        narInfo->fileSize = narCompressed->size();

        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now2 - now1).count();
        printMsg(lvlTalkative, "copying path '%1%' (%2% bytes, compressed %3$.1f%% in %4% ms) to binary cache",
            printStorePath(narInfo->path), narInfo->narSize,
            ((1.0 - (double) narCompressed->size() / nar->size()) * 100.0),
            duration);

        narInfo->url = "nar/" + narInfo->fileHash.to_string(Base32, false) + ".nar"
            + (compression == "xz" ? ".xz" :
                compression == "bzip2" ? ".bz2" :
                compression == "br" ? ".br" :
                "");

        /* Atomically write the NAR file. */
        if (repair || !fileExists(narInfo->url)) {
            stats.narWrite++;
            upsertFile(narInfo->url, *narCompressed, "application/x-nix-nar");
        } else
            stats.narWriteAverted++;

        stats.narWriteBytes += nar->size();
        stats.narWriteCompressedBytes += narCompressed->size();
        stats.narWriteCompressionTimeMs += duration;

        /* Atomically write the NAR info file.*/
        if (secretKey) narInfo->sign(*this, *secretKey);

        writeNarInfo(narInfo);

        stats.narInfoWrite++;
    }

    bool isValidPathUncached(const StorePath & storePath) override
    {
        return fileExists(narInfoFileFor(storePath));
    }

    void narFromPath(const StorePath & storePath, Sink & sink) override
    {
        auto info = queryPathInfo(storePath).cast<const NarInfo>();

        uint64_t narSize = 0;

        LambdaSink wrapperSink([&](const unsigned char * data, size_t len) {
            sink(data, len);
            narSize += len;
        });

        auto decompressor = makeDecompressionSink(info->compression, wrapperSink);

        try {
            getFile(info->url, *decompressor);
        } catch (NoSuchBinaryCacheFile & e) {
            throw SubstituteGone(e.what());
        }

        decompressor->finish();

        stats.narRead++;
        //stats.narReadCompressedBytes += nar->size(); // FIXME
        stats.narReadBytes += narSize;
    }

    void queryPathInfoUncached(const StorePath & storePath,
        Callback<std::shared_ptr<const ValidPathInfo>> callback) noexcept override
    {
        auto uri = getUri();
        auto storePathS = printStorePath(storePath);
        auto act = std::make_shared<Activity>(*logger, lvlTalkative, actQueryPathInfo,
            fmt("querying info about '%s' on '%s'", storePathS, uri), Logger::Fields{storePathS, uri});
        PushActivity pact(act->id);

        auto narInfoFile = narInfoFileFor(storePath);

        auto callbackPtr = std::make_shared<decltype(callback)>(std::move(callback));

        getFile(narInfoFile,
            {[=](std::future<std::shared_ptr<std::string>> fut) {
                try {
                    auto data = fut.get();

                    if (!data) return (*callbackPtr)(nullptr);

                    stats.narInfoRead++;

                    (*callbackPtr)((std::shared_ptr<ValidPathInfo>)
                        std::make_shared<NarInfo>(*this, *data, narInfoFile));

                    (void) act; // force Activity into this lambda to ensure it stays alive
                } catch (...) {
                    callbackPtr->rethrow();
                }
            }});
    }

    StorePath addToStore(const string & name, const Path & srcPath,
        FileIngestionMethod method, HashType hashAlgo, PathFilter & filter, RepairFlag repair) override
    {
        // FIXME: some cut&paste from LocalStore::addToStore().

        /* Read the whole path into memory. This is not a very scalable
           method for very large paths, but `copyPath' is mainly used for
           small files. */
        StringSink sink;
        Hash h;
        if (method == FileIngestionMethod::Recursive) {
            dumpPath(srcPath, sink, filter);
            h = hashString(hashAlgo, *sink.s);
        } else {
            auto s = readFile(srcPath);
            dumpString(s, sink);
            h = hashString(hashAlgo, s);
        }

        ValidPathInfo info(makeFixedOutputPath(method, h, name));

        auto source = StringSource { *sink.s };
        addToStore(info, source, repair, CheckSigs, nullptr);

        return std::move(info.path);
    }

    StorePath addTextToStore(const string & name, const string & s,
        const StorePathSet & references, RepairFlag repair) override
    {
        ValidPathInfo info(computeStorePathForText(name, s, references));
        info.references = cloneStorePathSet(references);

        if (repair || !isValidPath(info.path)) {
            StringSink sink;
            dumpString(s, sink);
            auto source = StringSource { *sink.s };
            addToStore(info, source, repair, CheckSigs, nullptr);
        }

        return std::move(info.path);
    }

    void addSignatures(const StorePath & storePath, const StringSet & sigs) override
    {
        /* Note: this is inherently racy since there is no locking on
           binary caches. In particular, with S3 this unreliable, even
           when addSignatures() is called sequentially on a path, because
           S3 might return an outdated cached version. */

        auto narInfo = make_ref<NarInfo>((NarInfo &) *queryPathInfo(storePath));

        narInfo->sigs.insert(sigs.begin(), sigs.end());

        auto narInfoFile = narInfoFileFor(narInfo->path);

        writeNarInfo(narInfo);
    }

    std::shared_ptr<std::string> getBuildLog(const StorePath & path) override
    {
        auto drvPath = path.clone();

        if (!path.isDerivation()) {
            try {
                auto info = queryPathInfo(path);
                // FIXME: add a "Log" field to .narinfo
                if (!info->deriver) return nullptr;
                drvPath = info->deriver->clone();
            } catch (InvalidPath &) {
                return nullptr;
            }
        }

        auto logPath = "log/" + std::string(baseNameOf(printStorePath(drvPath)));

        debug("fetching build log from binary cache '%s/%s'", getUri(), logPath);

        return getFile(logPath);
    }

    BuildResult buildDerivation(const StorePath & drvPath, const BasicDerivation & drv,
        BuildMode buildMode) override
    { unsupported("buildDerivation"); }

    void ensurePath(const StorePath & path) override
    { unsupported("ensurePath"); }

    std::optional<StorePath> queryPathFromHashPart(const std::string & hashPart) override
    { unsupported("queryPathFromHashPart"); }

};

static RegisterStoreImplementation regStore([](
    const std::string & uri, const Store::Params & params)
    -> std::shared_ptr<Store>
{
    if (uri.substr(0, strlen("ipfs://")) != "ipfs://" &&
        uri.substr(0, strlen("ipns://")) != "ipns://")
        return 0;
    auto store = std::make_shared<IPFSBinaryCacheStore>(params, uri);
    return store;
});

}
