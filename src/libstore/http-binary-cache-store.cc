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

    struct State
    {
        bool enabled = true;
        std::chrono::steady_clock::time_point disabledUntil;
    };

    Sync<State> _state;

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
                throw Error("'%s' does not appear to be a binary cache", cacheUri);
            }
            diskCache->createCache(cacheUri, storeDir, wantMassQuery_, priority);
        }
    }

protected:

    void maybeDisable()
    {
        auto state(_state.lock());
        if (state->enabled && settings.tryFallback) {
            int t = 60;
            printError("disabling binary cache '%s' for %s seconds", getUri(), t);
            state->enabled = false;
            state->disabledUntil = std::chrono::steady_clock::now() + std::chrono::seconds(t);
        }
    }

    void checkEnabled()
    {
        auto state(_state.lock());
        if (state->enabled) return;
        if (std::chrono::steady_clock::now() > state->disabledUntil) {
            state->enabled = true;
            debug("re-enabling binary cache '%s'", getUri());
            return;
        }
        throw SubstituterDisabled("substituter '%s' is disabled", getUri());
    }

    bool fileExists(const std::string & path) override
    {
        checkEnabled();

        try {
            DownloadRequest request(cacheUri + "/" + path);
            request.head = true;
            getDownloader()->download(request);
            return true;
        } catch (DownloadError & e) {
            /* S3 buckets return 403 if a file doesn't exist and the
               bucket is unlistable, so treat 403 as 404. */
            if (e.error == Downloader::NotFound || e.error == Downloader::Forbidden)
                return false;
            maybeDisable();
            throw;
        }
    }

    void upsertFile(const std::string & path,
        const std::string & data,
        const std::string & mimeType) override
    {
        auto req = DownloadRequest(cacheUri + "/" + path);
        req.data = std::make_shared<string>(data); // FIXME: inefficient
        req.mimeType = mimeType;
        try {
            getDownloader()->download(req);
        } catch (DownloadError & e) {
            throw UploadToHTTP("while uploading to HTTP binary cache at '%s': %s", cacheUri, e.msg());
        }
    }

    DownloadRequest makeRequest(const std::string & path)
    {
        DownloadRequest request(cacheUri + "/" + path);
        return request;
    }

    void getFile(const std::string & path, Sink & sink) override
    {
        checkEnabled();
        auto request(makeRequest(path));
        try {
            getDownloader()->download(std::move(request), sink);
        } catch (DownloadError & e) {
            if (e.error == Downloader::NotFound || e.error == Downloader::Forbidden)
                throw NoSuchBinaryCacheFile("file '%s' does not exist in binary cache '%s'", path, getUri());
            maybeDisable();
            throw;
        }
    }

    void getFile(const std::string & path,
        Callback<std::shared_ptr<std::string>> callback) noexcept override
    {
        checkEnabled();

        auto request(makeRequest(path));

        auto callbackPtr = std::make_shared<decltype(callback)>(std::move(callback));

        getDownloader()->enqueueDownload(request,
            {[callbackPtr, this](std::future<DownloadResult> result) {
                try {
                    (*callbackPtr)(result.get().data);
                } catch (DownloadError & e) {
                    if (e.error == Downloader::NotFound || e.error == Downloader::Forbidden)
                        return (*callbackPtr)(std::shared_ptr<std::string>());
                    maybeDisable();
                    callbackPtr->rethrow();
                } catch (...) {
                    callbackPtr->rethrow();
                }
            }});
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

