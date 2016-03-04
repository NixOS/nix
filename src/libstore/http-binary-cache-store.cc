#include "binary-cache-store.hh"
#include "download.hh"
#include "globals.hh"

namespace nix {

class HttpBinaryCacheStore : public BinaryCacheStore
{
private:

    Path cacheUri;

    ref<Downloader> downloader;

public:

    HttpBinaryCacheStore(std::shared_ptr<Store> localStore,
        const Path & secretKeyFile, const Path & _cacheUri)
        : BinaryCacheStore(localStore, secretKeyFile)
        , cacheUri(_cacheUri)
        , downloader(makeDownloader())
    {
        if (cacheUri.back() == '/')
            cacheUri.pop_back();
    }

    void init() override
    {
        // FIXME: do this lazily?
        if (!fileExists("nix-cache-info"))
            throw Error(format("‘%s’ does not appear to be a binary cache") % cacheUri);
    }

protected:

    bool fileExists(const std::string & path) override
    {
        try {
            DownloadOptions options;
            options.showProgress = DownloadOptions::no;
            options.head = true;
            downloader->download(cacheUri + "/" + path, options);
            return true;
        } catch (DownloadError & e) {
            if (e.error == Downloader::NotFound)
                return false;
            throw;
        }
    }

    void upsertFile(const std::string & path, const std::string & data)
    {
        throw Error("uploading to an HTTP binary cache is not supported");
    }

    std::string getFile(const std::string & path) override
    {
        DownloadOptions options;
        options.showProgress = DownloadOptions::no;
        return downloader->download(cacheUri + "/" + path, options).data;
    }

};

static RegisterStoreImplementation regStore([](const std::string & uri) -> std::shared_ptr<Store> {
    if (std::string(uri, 0, 7) != "http://" &&
        std::string(uri, 0, 8) != "https://") return 0;
    auto store = std::make_shared<HttpBinaryCacheStore>(std::shared_ptr<Store>(0),
        settings.get("binary-cache-secret-key-file", string("")),
        uri);
    store->init();
    return store;
});

}

