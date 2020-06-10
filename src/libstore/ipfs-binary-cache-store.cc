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

    std::string ipfsPath;

    struct State
    {
        bool inProgressUpsert = false;
    };
    Sync<State> _state;

public:

    IPFSBinaryCacheStore(
      const Params & params, const Path & _cacheUri)
            : BinaryCacheStore(params)
            , cacheUri(_cacheUri)
    {
        if (cacheUri.back() == '/')
            cacheUri.pop_back();

        if (hasPrefix(cacheUri, "ipfs://"))
            ipfsPath = "/ipfs/" + std::string(cacheUri, 7);
        else if (hasPrefix(cacheUri, "ipns://"))
            ipfsPath = "/ipns/" + std::string(cacheUri, 7);
        else
            throw Error("unknown IPFS URI '%s'", cacheUri);

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

        // root should already exist
        if (!fileExists("") && hasPrefix(ipfsPath, "/ipfs/"))
            throw Error("path '%s' is not found", ipfsPath);
    }

    std::string getUri() override
    {
        return cacheUri;
    }

    void init() override
    {
        std::string cacheInfoFile = "nix-cache-info";
        if (!fileExists(cacheInfoFile)) {
            upsertFile(cacheInfoFile, "StoreDir: " + storeDir + "\n", "text/x-nix-cache-info");
        }
        BinaryCacheStore::init();
    }

protected:

    bool fileExists(const std::string & path) override
    {
        auto uri = daemonUri + "/api/v0/object/stat?offline=true&arg=" + getFileTransfer()->urlEncode(ipfsPath + "/" + path);

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

    void upsertFile(const std::string & path, const std::string & data, const std::string & mimeType) override
    {
        if (hasPrefix(ipfsPath, "/ipfs/"))
            throw Error("%s is immutable, cannot modify", ipfsPath);

        // TODO: use callbacks

        auto req1 = FileTransferRequest(daemonUri + "/api/v0/add");
        req1.data = std::make_shared<string>(data);
        req1.post = true;
        req1.tries = 1;
        try {
            auto res1 = getFileTransfer()->upload(req1);
            auto json1 = nlohmann::json::parse(*res1.data);

            auto addedPath = "/ipfs/" + (std::string) json1["Hash"];

            auto state(_state.lock());

            if (state->inProgressUpsert)
                throw Error("a modification to the IPNS is already in progress");

            state->inProgressUpsert = true;

            auto uri1 = daemonUri + "/api/v0/object/patch/add-link?offline=true&create=true";
            uri1 += "&arg=" + getFileTransfer()->urlEncode(ipfsPath);
            uri1 += "&arg=" + getFileTransfer()->urlEncode(path);
            uri1 += "&arg=" + getFileTransfer()->urlEncode(addedPath);

            auto req2 = FileTransferRequest(uri1);
            req2.post = true;
            req2.tries = 1;
            auto res2 = getFileTransfer()->download(req2);
            auto json2 = nlohmann::json::parse(*res2.data);

            auto newRoot = json2["Hash"];

            auto uri2 = daemonUri + "/api/v0/name/publish?offline=true&arg=" + getFileTransfer()->urlEncode(newRoot);
            uri2 += "&key=" + std::string(ipfsPath, 6);

            auto req3 = FileTransferRequest(uri2);
            req3.post = true;
            req3.tries = 1;
            getFileTransfer()->download(req3);

            state->inProgressUpsert = false;
        } catch (FileTransferError & e) {
            throw UploadToIPFS("while uploading to IPFS binary cache at '%s': %s", cacheUri, e.msg());
        }
    }

    void getFile(const std::string & path,
        Callback<std::shared_ptr<std::string>> callback) noexcept override
    {
        auto uri = daemonUri + "/api/v0/cat?offline=true&arg=" + getFileTransfer()->urlEncode(ipfsPath + "/" + path);

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
