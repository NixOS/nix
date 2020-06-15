#include <cstring>
#include <nlohmann/json.hpp>

#include "binary-cache-store.hh"
#include "filetransfer.hh"
#include "nar-info-disk-cache.hh"

namespace nix {

MakeError(UploadToIPFS, Error);

class IPFSBinaryCacheStore : public BinaryCacheStore
{

private:

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
            : BinaryCacheStore(params)
            , cacheUri(_cacheUri)
    {
        auto state(_state.lock());

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

    void init() override
    {
        std::string cacheInfoFile = "nix-cache-info";
        if (!fileExists(cacheInfoFile))
            upsertFile(cacheInfoFile, "StoreDir: " + storeDir + "\n", "text/x-nix-cache-info");
        BinaryCacheStore::init();
    }

private:

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

    bool fileExists(const std::string & path) override
    {
        return ipfsObjectExists(getIpfsRootDir() + "/" + path);
    }

private:

    // Resolve the IPNS name to an IPFS object
    static std::string resolveIPNSName(std::string ipnsPath, bool offline) {
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

protected:

    void upsertFile(const std::string & path, const std::string & data, const std::string & mimeType) override
    {
        try {
            addLink(path, "/ipfs/" + addFile(data));
        } catch (FileTransferError & e) {
            throw UploadToIPFS("while uploading to IPFS binary cache at '%s': %s", cacheUri, e.msg());
        }
    }

    void getFile(const std::string & path,
        Callback<std::shared_ptr<std::string>> callback) noexcept override
    {
        getIpfsObject(getIpfsPath() + "/" + path, std::move(callback));
    }

private:

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

};

static RegisterStoreImplementation regStore([](
    const std::string & uri, const Store::Params & params)
    -> std::shared_ptr<Store>
{
    if (uri.substr(0, strlen("ipfs://")) != "ipfs://" &&
        uri.substr(0, strlen("ipns://")) != "ipns://")
        return 0;
    auto store = std::make_shared<IPFSBinaryCacheStore>(params, uri);
    store->init();
    return store;
});

}
