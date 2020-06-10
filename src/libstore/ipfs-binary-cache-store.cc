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
    std::optional<string> ipnsPath;

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

        if (hasPrefix(cacheUri, "ipfs://"))
            state->ipfsPath = "/ipfs/" + std::string(cacheUri, 7);
        else if (hasPrefix(cacheUri, "ipns://"))
            ipnsPath = "/ipns/" + std::string(cacheUri, 7);
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
        if (ipnsPath) {
            debug("Resolving IPFS object of '%s', this could take a while.", *ipnsPath);
            auto uri = daemonUri + "/api/v0/name/resolve?offline=true&arg=" + getFileTransfer()->urlEncode(*ipnsPath);
            FileTransferRequest request(uri);
            request.post = true;
            request.tries = 1;
            auto res = getFileTransfer()->download(request);
            auto json = nlohmann::json::parse(*res.data);
            if (json.find("Path") == json.end())
                throw Error("daemon for IPFS is not running properly");
            state->ipfsPath = json["Path"];
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

protected:

    bool fileExists(const std::string & path) override
    {
        auto uri = daemonUri + "/api/v0/object/stat?arg=" + getFileTransfer()->urlEncode(getIpfsPath() + "/" + path);

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

    // IPNS publish can be slow, we try to do it rarely.
    void sync() override
    {
        if (!ipnsPath)
            return;

        auto state(_state.lock());

        debug("Publishing '%s' to '%s', this could take a while.", state->ipfsPath, *ipnsPath);

        auto uri = daemonUri + "/api/v0/name/publish?offline=true&arg=" + getFileTransfer()->urlEncode(state->ipfsPath);
        uri += "&key=" + std::string(*ipnsPath, 6);

        auto req = FileTransferRequest(uri);
        req.post = true;
        req.tries = 1;
        getFileTransfer()->download(req);
    }

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

    void upsertFile(const std::string & path, const std::string & data, const std::string & mimeType) override
    {
        // TODO: use callbacks

        auto req = FileTransferRequest(daemonUri + "/api/v0/add");
        req.data = std::make_shared<string>(data);
        req.post = true;
        req.tries = 1;
        try {
            auto res = getFileTransfer()->upload(req);
            auto json = nlohmann::json::parse(*res.data);
            addLink(path, "/ipfs/" + (std::string) json["Hash"]);
        } catch (FileTransferError & e) {
            throw UploadToIPFS("while uploading to IPFS binary cache at '%s': %s", cacheUri, e.msg());
        }
    }

    void getFile(const std::string & path,
        Callback<std::shared_ptr<std::string>> callback) noexcept override
    {
        auto uri = daemonUri + "/api/v0/cat?arg=" + getFileTransfer()->urlEncode(getIpfsPath() + "/" + path);

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
