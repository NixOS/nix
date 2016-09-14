#include "binary-cache-store.hh"
#include "download.hh"
#include "globals.hh"
#include "nar-info-disk-cache.hh"

namespace nix {

MakeError(UploadToHTTP, Error);

class HttpBinaryCacheStore : public BinaryCacheStore
{
private:

    Path cacheUri;

public:

    HttpBinaryCacheStore(
        const Params & params, const Path & _cacheUri)
        : BinaryCacheStore(params)
        , cacheUri(_cacheUri)
    {
        if (cacheUri.back() == '/')
            cacheUri.pop_back();

        diskCache = getNarInfoDiskCache();
    }

    std::string getUri() override
    {
        return cacheUri;
    }

    void init() override
    {
        // FIXME: do this lazily?
        if (!diskCache->cacheExists(cacheUri, wantMassQuery_, priority)) {
            try {
                BinaryCacheStore::init();
            } catch (UploadToHTTP &) {
                throw Error(format("‘%s’ does not appear to be a binary cache") % cacheUri);
            }
            diskCache->createCache(cacheUri, storeDir, wantMassQuery_, priority);
        }
    }

protected:

    bool fileExists(const std::string & path) override
    {
        try {
            DownloadRequest request(cacheUri + "/" + path);
            request.showProgress = DownloadRequest::no;
            request.head = true;
            request.tries = 5;
            getDownloader()->download(request);
            return true;
        } catch (DownloadError & e) {
            /* S3 buckets return 403 if a file doesn't exist and the
               bucket is unlistable, so treat 403 as 404. */
            if (e.error == Downloader::NotFound || e.error == Downloader::Forbidden)
                return false;
            throw;
        }
    }

    void upsertFile(const std::string & path, const std::string & data) override
    {
        throw UploadToHTTP("uploading to an HTTP binary cache is not supported");
    }

    std::shared_ptr<std::string> getFile(const std::string & path) override
    {
        DownloadRequest request(cacheUri + "/" + path);
        request.showProgress = DownloadRequest::no;
        request.tries = 8;
        try {
            return getDownloader()->download(request).data;
        } catch (DownloadError & e) {
            if (e.error == Downloader::NotFound || e.error == Downloader::Forbidden)
                return 0;
            throw;
        }
    }

};

static RegisterStoreImplementation regStore([](
    const std::string & uri, const Store::Params & params)
    -> std::shared_ptr<Store>
{
    if (std::string(uri, 0, 7) != "http://" &&
        std::string(uri, 0, 8) != "https://" &&
        (getEnv("_NIX_FORCE_HTTP_BINARY_CACHE_STORE") != "1" || std::string(uri, 0, 7) != "file://")
        ) return 0;
    auto store = std::make_shared<HttpBinaryCacheStore>(params, uri);
    store->init();
    return store;
});

}

