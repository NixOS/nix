#include "http-binary-cache-store.hh"
#include "filetransfer.hh"
#include "globals.hh"
#include "nar-info-disk-cache.hh"
#include "callback.hh"
#include "store-registration.hh"

namespace nix {

MakeError(UploadToHTTP, Error);

std::set<std::string> HttpBinaryCacheStoreConfig::uriSchemes()
{
    static bool forceHttp = getEnv("_NIX_FORCE_HTTP") == "1";
    auto ret = std::set<std::string>({"http", "https"});
    if (forceHttp)
        ret.insert("file");
    return ret;
}

HttpBinaryCacheStoreConfig::HttpBinaryCacheStoreConfig(
    std::string_view scheme,
    std::string_view _cacheUri,
    const StoreReference::Params & params)
    : Store::Config{params}
    , BinaryCacheStoreConfig{*this, params}
    , cacheUri(
        std::string { scheme }
        + "://"
        + (!_cacheUri.empty()
            ? _cacheUri
            : throw UsageError("`%s` Store requires a non-empty authority in Store URL", scheme)))
{
    while (!cacheUri.empty() && cacheUri.back() == '/')
        cacheUri.pop_back();
}


std::string HttpBinaryCacheStoreConfig::doc()
{
    return
      #include "http-binary-cache-store.md"
      ;
}


class HttpBinaryCacheStore :
    public virtual BinaryCacheStore
{
    struct State
    {
        bool enabled = true;
        std::chrono::steady_clock::time_point disabledUntil;
    };

    Sync<State> _state;

public:

    using Config = HttpBinaryCacheStoreConfig;

    ref<const Config> config;

    HttpBinaryCacheStore(ref<const Config> config)
        : Store{*config}
        , BinaryCacheStore{*config}
        , config{config}
    {
        diskCache = getNarInfoDiskCache();
    }

    std::string getUri() override
    {
        return config->cacheUri;
    }

    void init() override
    {
        // FIXME: do this lazily?
        if (auto cacheInfo = diskCache->upToDateCacheExists(config->cacheUri)) {
            resolvedSubstConfig.wantMassQuery.value =
                config->storeConfig.wantMassQuery.optValue.value_or(cacheInfo->wantMassQuery);
            resolvedSubstConfig.priority.value =
                config->storeConfig.priority.optValue.value_or(cacheInfo->priority);
        } else {
            try {
                BinaryCacheStore::init();
            } catch (UploadToHTTP &) {
                throw Error("'%s' does not appear to be a binary cache", config->cacheUri);
            }
            diskCache->createCache(
                config->cacheUri, storeDir, resolvedSubstConfig.wantMassQuery, resolvedSubstConfig.priority);
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
            FileTransferRequest request(makeRequest(path));
            request.head = true;
            getFileTransfer()->download(request);
            return true;
        } catch (FileTransferError & e) {
            /* S3 buckets return 403 if a file doesn't exist and the
               bucket is unlistable, so treat 403 as 404. */
            if (e.error == FileTransfer::NotFound || e.error == FileTransfer::Forbidden)
                return false;
            maybeDisable();
            throw;
        }
    }

    void upsertFile(const std::string & path,
        std::shared_ptr<std::basic_iostream<char>> istream,
        const std::string & mimeType) override
    {
        auto req = makeRequest(path);
        req.data = StreamToSourceAdapter(istream).drain();
        req.mimeType = mimeType;
        try {
            getFileTransfer()->upload(req);
        } catch (FileTransferError & e) {
            throw UploadToHTTP("while uploading to HTTP binary cache at '%s': %s", config->cacheUri, e.msg());
        }
    }

    FileTransferRequest makeRequest(const std::string & path)
    {
        return FileTransferRequest(
            hasPrefix(path, "https://") || hasPrefix(path, "http://") || hasPrefix(path, "file://")
            ? path
            : config->cacheUri + "/" + path);

    }

    void getFile(const std::string & path, Sink & sink) override
    {
        checkEnabled();
        auto request(makeRequest(path));
        try {
            getFileTransfer()->download(std::move(request), sink);
        } catch (FileTransferError & e) {
            if (e.error == FileTransfer::NotFound || e.error == FileTransfer::Forbidden)
                throw NoSuchBinaryCacheFile("file '%s' does not exist in binary cache '%s'", path, getUri());
            maybeDisable();
            throw;
        }
    }

    void getFile(const std::string & path,
        Callback<std::optional<std::string>> callback) noexcept override
    {
        try {
            checkEnabled();
        } catch (...) {
            callback.rethrow();
            return;
        }

        auto request(makeRequest(path));

        auto callbackPtr = std::make_shared<decltype(callback)>(std::move(callback));

        getFileTransfer()->enqueueFileTransfer(request,
            {[callbackPtr, this](std::future<FileTransferResult> result) {
                try {
                    (*callbackPtr)(std::move(result.get().data));
                } catch (FileTransferError & e) {
                    if (e.error == FileTransfer::NotFound || e.error == FileTransfer::Forbidden)
                        return (*callbackPtr)({});
                    maybeDisable();
                    callbackPtr->rethrow();
                } catch (...) {
                    callbackPtr->rethrow();
                }
            }});
    }

    /**
     * This isn't actually necessary read only. We support "upsert" now, so we
     * have a notion of authentication via HTTP POST/PUT.
     *
     * For now, we conservatively say we don't know.
     *
     * \todo try to expose our HTTP authentication status.
     */
    std::optional<TrustedFlag> isTrustedClient() override
    {
        return std::nullopt;
    }
};

ref<Store> HttpBinaryCacheStore::Config::openStore() const
{
    return make_ref<HttpBinaryCacheStore>(ref{shared_from_this()});
}

static RegisterStoreImplementation<HttpBinaryCacheStore::Config> regHttpBinaryCacheStore;

}
