#include "nix/store/http-binary-cache-store.hh"
#include "nix/store/filetransfer.hh"
#include "nix/store/globals.hh"
#include "nix/store/nar-info-disk-cache.hh"
#include "nix/util/callback.hh"
#include "nix/store/store-registration.hh"

namespace nix {

MakeError(UploadToHTTP, Error);

StringSet HttpBinaryCacheStoreConfig::uriSchemes()
{
    static bool forceHttp = getEnv("_NIX_FORCE_HTTP") == "1";
    auto ret = StringSet{"http", "https"};
    if (forceHttp)
        ret.insert("file");
    return ret;
}

HttpBinaryCacheStoreConfig::HttpBinaryCacheStoreConfig(
    std::string_view scheme, std::string_view _cacheUri, const Params & params)
    : StoreConfig(params)
    , BinaryCacheStoreConfig(params)
    , cacheUri(parseURL(
          std::string{scheme} + "://"
          + (!_cacheUri.empty() ? _cacheUri
                                : throw UsageError("`%s` Store requires a non-empty authority in Store URL", scheme))))
{
    while (!cacheUri.path.empty() && cacheUri.path.back() == '/')
        cacheUri.path.pop_back();
}

StoreReference HttpBinaryCacheStoreConfig::getReference() const
{
    return {
        .variant =
            StoreReference::Specified{
                .scheme = cacheUri.scheme,
                .authority = (cacheUri.authority ? cacheUri.authority->to_string() : "") + cacheUri.path,
            },
        .params = cacheUri.query,
    };
}

std::string HttpBinaryCacheStoreConfig::doc()
{
    return
#include "http-binary-cache-store.md"
        ;
}

class HttpBinaryCacheStore : public virtual BinaryCacheStore
{
    struct State
    {
        bool enabled = true;
        std::chrono::steady_clock::time_point disabledUntil;
    };

    Sync<State> _state;

public:

    using Config = HttpBinaryCacheStoreConfig;

    ref<Config> config;

    HttpBinaryCacheStore(ref<Config> config)
        : Store{*config} // TODO it will actually mutate the configuration
        , BinaryCacheStore{*config}
        , config{config}
    {
        diskCache = getNarInfoDiskCache();
    }

    void init() override
    {
        // FIXME: do this lazily?
        if (auto cacheInfo = diskCache->upToDateCacheExists(config->cacheUri.to_string())) {
            config->wantMassQuery.setDefault(cacheInfo->wantMassQuery);
            config->priority.setDefault(cacheInfo->priority);
        } else {
            try {
                BinaryCacheStore::init();
            } catch (UploadToHTTP &) {
                throw Error("'%s' does not appear to be a binary cache", config->cacheUri.to_string());
            }
            diskCache->createCache(
                config->cacheUri.to_string(), config->storeDir, config->wantMassQuery, config->priority);
        }
    }

protected:

    void maybeDisable()
    {
        auto state(_state.lock());
        if (state->enabled && settings.tryFallback) {
            int t = 60;
            printError("disabling binary cache '%s' for %s seconds", config->getHumanReadableURI(), t);
            state->enabled = false;
            state->disabledUntil = std::chrono::steady_clock::now() + std::chrono::seconds(t);
        }
    }

    void checkEnabled()
    {
        auto state(_state.lock());
        if (state->enabled)
            return;
        if (std::chrono::steady_clock::now() > state->disabledUntil) {
            state->enabled = true;
            debug("re-enabling binary cache '%s'", config->getHumanReadableURI());
            return;
        }
        throw SubstituterDisabled("substituter '%s' is disabled", config->getHumanReadableURI());
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

    void upsertFile(
        const std::string & path,
        std::shared_ptr<std::basic_iostream<char>> istream,
        const std::string & mimeType) override
    {
        auto req = makeRequest(path);
        req.data = StreamToSourceAdapter(istream).drain();
        req.mimeType = mimeType;
        try {
            getFileTransfer()->upload(req);
        } catch (FileTransferError & e) {
            throw UploadToHTTP(
                "while uploading to HTTP binary cache at '%s': %s", config->cacheUri.to_string(), e.msg());
        }
    }

    FileTransferRequest makeRequest(const std::string & path)
    {
        /* FIXME path is not a path, but a full relative or absolute
           URL, e.g. we've seen in the wild NARINFO files have a URL
           field which is
           `nar/15f99rdaf26k39knmzry4xd0d97wp6yfpnfk1z9avakis7ipb9yg.nar?hash=zphkqn2wg8mnvbkixnl2aadkbn0rcnfj`
           (note the query param) and that gets passed here.

           What should actually happen is that we have two parsed URLs
           (if we support relative URLs), and then we combined them with
           a URL `operator/` which would be like
           `std::filesystem::path`'s equivalent operator, which properly
           combines the the URLs, whether the right is relative or
           absolute. */
        return FileTransferRequest(
            hasPrefix(path, "https://") || hasPrefix(path, "http://") || hasPrefix(path, "file://")
                ? path
                : config->cacheUri.to_string() + "/" + path);
    }

    void getFile(const std::string & path, Sink & sink) override
    {
        checkEnabled();
        auto request(makeRequest(path));
        try {
            getFileTransfer()->download(std::move(request), sink);
        } catch (FileTransferError & e) {
            if (e.error == FileTransfer::NotFound || e.error == FileTransfer::Forbidden)
                throw NoSuchBinaryCacheFile(
                    "file '%s' does not exist in binary cache '%s'", path, config->getHumanReadableURI());
            maybeDisable();
            throw;
        }
    }

    void getFile(const std::string & path, Callback<std::optional<std::string>> callback) noexcept override
    {
        auto callbackPtr = std::make_shared<decltype(callback)>(std::move(callback));

        try {
            checkEnabled();

            auto request(makeRequest(path));

            getFileTransfer()->enqueueFileTransfer(
                request, {[callbackPtr, this](std::future<FileTransferResult> result) {
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

        } catch (...) {
            callbackPtr->rethrow();
            return;
        }
    }

    std::optional<std::string> getNixCacheInfo() override
    {
        try {
            auto result = getFileTransfer()->download(makeRequest(cacheInfoFile));
            return result.data;
        } catch (FileTransferError & e) {
            if (e.error == FileTransfer::NotFound)
                return std::nullopt;
            maybeDisable();
            throw;
        }
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
    return make_ref<HttpBinaryCacheStore>(
        ref{// FIXME we shouldn't actually need a mutable config
            std::const_pointer_cast<HttpBinaryCacheStore::Config>(shared_from_this())});
}

static RegisterStoreImplementation<HttpBinaryCacheStore::Config> regHttpBinaryCacheStore;

} // namespace nix
