#include <cstring>

#include "binary-cache-store.hh"
#include "download.hh"
#include "nar-info-disk-cache.hh"
#include "ipfs.hh"

namespace nix {

MakeError(UploadToIPFS, Error);

class IPFSBinaryCacheStore : public BinaryCacheStore
{

private:

    std::string cacheUri;

    /* Host where a IPFS API can be reached (usually localhost) */
    std::string ipfsAPIHost;
    /* Port where a IPFS API can be reached (usually 5001) */
    uint16_t    ipfsAPIPort;
    /* Whether to use a IPFS Gateway instead of the API */
    bool        useIpfsGateway;
    /* Where to find a IPFS Gateway */
    std::string ipfsGatewayURL;

    std::string constructIPFSRequest(const std::string & path) {
        std::string uri;
        std::string ipfsPath = cacheUri + "/" + path;
        if (useIpfsGateway == false) {
          uri = ipfs::buildAPIURL(ipfsAPIHost, ipfsAPIPort) +
                "/cat" +
                ipfs::buildQuery({{"arg", ipfsPath}});
        } else {
          uri = ipfsGatewayURL + ipfsPath;
        }
        return uri;
    }

public:

    IPFSBinaryCacheStore(
      const Params & params, const Path & _cacheUri)
            : BinaryCacheStore(params)
            , cacheUri(_cacheUri)
            , ipfsAPIHost(get(params, "host", "127.0.0.1"))
            , ipfsAPIPort(std::stoi(get(params, "port", "5001")))
            , useIpfsGateway(get(params, "use_gateway", "0") == "1")
            , ipfsGatewayURL(get(params, "gateway", "https://ipfs.io"))
    {
        if (cacheUri.back() == '/')
            cacheUri.pop_back();
        /*
         * A cache is still useful since the IPFS API or
         * gateway may have a higher latency when not running on
         * localhost
         */
        diskCache = getNarInfoDiskCache();
    }

    std::string getUri() override
    {
        return cacheUri;
    }

    void init() override
    {
        if (!diskCache->cacheExists(cacheUri, wantMassQuery_, priority)) {
            try {
              BinaryCacheStore::init();
            } catch (UploadToIPFS &) {
              throw Error(format("‘%s’ does not appear to be a binary cache") % cacheUri);
            }
            diskCache->createCache(cacheUri, storeDir, wantMassQuery_, priority);
        }
    }

protected:

    bool fileExists(const std::string & path) override
    {
        /*
         * TODO: Try a local mount first, best to share code with
         * LocalBinaryCacheStore
         */

        /* TODO: perform ipfs ls instead instead of trying to fetch it */
        auto uri = constructIPFSRequest(path);
        try {
            DownloadRequest request(uri);
            request.showProgress = DownloadRequest::no;
            request.tries = 5;
            if (useIpfsGateway)
                request.head = true;
            getDownloader()->download(request);
            return true;
        } catch (DownloadError & e) {
            if (e.error == Downloader::NotFound)
                return false;
            throw;
        }
    }

    void upsertFile(const std::string & path, const std::string & data) override
    {
        throw UploadToIPFS("uploading to an IPFS binary cache is not supported");
    }

    void getFile(const std::string & path,
        std::function<void(std::shared_ptr<std::string>)> success,
        std::function<void(std::exception_ptr exc)> failure) override
    {
        /*
         * TODO: Try local mount first, best to share code with
         * LocalBinaryCacheStore
         */
        auto uri = constructIPFSRequest(path);
        DownloadRequest request(uri);
        request.showProgress = DownloadRequest::no;
        request.tries = 8;

        getDownloader()->enqueueDownload(request,
            [success](const DownloadResult & result) {
                success(result.data);
            },
            [success, failure](std::exception_ptr exc) {
                try {
                    std::rethrow_exception(exc);
                } catch (DownloadError & e) {
                    if (e.error == Downloader::NotFound)
                        return success(0);
                    failure(exc);
                } catch (...) {
                    failure(exc);
                }
            });
    }

};

static RegisterStoreImplementation regStore([](
    const std::string & uri, const Store::Params & params)
    -> std::shared_ptr<Store>
{
    /*
     * TODO: maybe use ipfs:/ fs:/ipfs/
     * https://github.com/ipfs/go-ipfs/issues/1678#issuecomment-157478515
     */
    if (uri.substr(0, strlen("/ipfs/")) != "/ipfs/" &&
        uri.substr(0, strlen("/ipns/")) != "/ipns/")
        return 0;
    auto store = std::make_shared<IPFSBinaryCacheStore>(params, uri);
    store->init();
    return store;
});

}
