#include "nix/store/http-binary-cache-store.hh"
#include "nix/store/filetransfer.hh"
#include "nix/store/globals.hh"
#include "nix/store/nar-info-disk-cache.hh"
#include "nix/store/sqlite.hh"
#include "nix/util/callback.hh"
#include "nix/store/store-registration.hh"
#include "nix/store/globals.hh"

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

HttpBinaryCacheStoreConfig::HttpBinaryCacheStoreConfig(ParsedURL _cacheUri, const Params & params)
    : StoreConfig(params)
    , BinaryCacheStoreConfig(params)
    , cacheUri(std::move(_cacheUri))
{
    if (!uriSchemes().contains("file") && (!cacheUri.authority || cacheUri.authority->host.empty()))
        throw UsageError("`%s` Store requires a non-empty authority in Store URL", cacheUri.scheme);
    while (!cacheUri.path.empty() && cacheUri.path.back() == "")
        cacheUri.path.pop_back();
}

StoreReference HttpBinaryCacheStoreConfig::getReference() const
{
    return {
        .variant =
            StoreReference::Specified{
                .scheme = cacheUri.scheme,
                .authority = cacheUri.renderAuthorityAndPath(),
            },
        .params = getQueryParams(),
    };
}

std::string HttpBinaryCacheStoreConfig::doc()
{
    return
#include "http-binary-cache-store.md"
        ;
}

HttpBinaryCacheStore::HttpBinaryCacheStore(ref<Config> config, ref<FileTransfer> fileTransfer)
    : Store{*config} // TODO it will actually mutate the configuration
    , BinaryCacheStore{*config}
    , fileTransfer{fileTransfer}
    , config{config}
{
    diskCache = NarInfoDiskCache::get(settings.getNarInfoDiskCacheSettings(), {.useWAL = settings.useSQLiteWAL});
}

void HttpBinaryCacheStore::init()
{
    // FIXME: do this lazily?
    // For consistent cache key handling, use the reference without parameters
    // This matches what's used in Store::queryPathInfo() lookups
    auto cacheKey = config->getReference().render(/*withParams=*/false);

    if (auto cacheInfo = diskCache->upToDateCacheExists(cacheKey)) {
        config->wantMassQuery.setDefault(cacheInfo->wantMassQuery);
        config->priority.setDefault(cacheInfo->priority);
    } else {
        try {
            BinaryCacheStore::init();
        } catch (UploadToHTTP &) {
            throw Error("'%s' does not appear to be a binary cache", config->cacheUri.to_string());
        }
        diskCache->createCache(cacheKey, config->storeDir, config->wantMassQuery, config->priority);
    }
}

std::optional<CompressionAlgo> HttpBinaryCacheStore::getCompressionMethod(const std::string & path)
{
    if (hasSuffix(path, ".narinfo") && config->narinfoCompression.get())
        return config->narinfoCompression;
    else if (hasSuffix(path, ".ls") && config->lsCompression.get())
        return config->lsCompression;
    else if (hasPrefix(path, "log/") && config->logCompression.get())
        return config->logCompression;
    else
        return std::nullopt;
}

void HttpBinaryCacheStore::maybeDisable()
{
    auto state(_state.lock());
    if (state->enabled && settings.getWorkerSettings().tryFallback) {
        int t = 60;
        printError("disabling binary cache '%s' for %s seconds", config->getHumanReadableURI(), t);
        state->enabled = false;
        state->disabledUntil = std::chrono::steady_clock::now() + std::chrono::seconds(t);
    }
}

void HttpBinaryCacheStore::checkEnabled()
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

bool HttpBinaryCacheStore::fileExists(const std::string & path)
{
    checkEnabled();

    try {
        FileTransferRequest request(makeRequest(path));
        request.method = HttpMethod::Head;
        fileTransfer->download(request);
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

void HttpBinaryCacheStore::upload(
    std::string_view path,
    RestartableSource & source,
    uint64_t sizeHint,
    std::string_view mimeType,
    std::optional<Headers> headers)
{
    auto req = makeRequest(path);
    req.method = HttpMethod::Put;

    if (headers) {
        req.headers.reserve(req.headers.size() + headers->size());
        std::ranges::move(std::move(*headers), std::back_inserter(req.headers));
    }

    req.data = {sizeHint, source};
    req.mimeType = mimeType;

    fileTransfer->upload(req);
}

void HttpBinaryCacheStore::upsertFile(
    const std::string & path, RestartableSource & source, const std::string & mimeType, uint64_t sizeHint)
{
    try {
        if (auto compressionMethod = getCompressionMethod(path)) {
            CompressedSource compressed(source, *compressionMethod);
            /* TODO: Validate that this is a valid content encoding. We probably shouldn't set non-standard values here.
             */
            Headers headers = {{"Content-Encoding", showCompressionAlgo(*compressionMethod)}};
            upload(path, compressed, compressed.size(), mimeType, std::move(headers));
        } else {
            upload(path, source, sizeHint, mimeType, std::nullopt);
        }
    } catch (FileTransferError & e) {
        UploadToHTTP err(e.message());
        err.addTrace({}, "while uploading to HTTP binary cache at '%s'", config->cacheUri.to_string());
        throw err;
    }
}

FileTransferRequest HttpBinaryCacheStore::makeRequest(std::string_view path)
{
    /* Otherwise the last path fragment will get discarded. */
    auto cacheUriWithTrailingSlash = config->cacheUri;
    if (!cacheUriWithTrailingSlash.path.empty())
        cacheUriWithTrailingSlash.path.push_back("");

    /* path is not a path, but a full relative or absolute
       URL, e.g. we've seen in the wild NARINFO files have a URL
       field which is
       `nar/15f99rdaf26k39knmzry4xd0d97wp6yfpnfk1z9avakis7ipb9yg.nar?hash=wvx0nans273vb7b0cjlplsmr2z905hwd`
       (note the query param) and that gets passed here. */
    auto result = parseURLRelative(path, cacheUriWithTrailingSlash);

    /* For S3 URLs, preserve query parameters from the base URL when the
       relative path doesn't have its own query parameters. This is needed
       to preserve S3-specific parameters like endpoint and region. */
    if (config->cacheUri.scheme == "s3" && result.query.empty()) {
        result.query = config->cacheUri.query;
    }

    FileTransferRequest request(result);

    /* Only use the specified SSL certificate and private key if the resolved URL names the same
       authority and uses the same protocol. */
    if (result.scheme == config->cacheUri.scheme && result.authority == config->cacheUri.authority) {
        if (const auto & cert = config->tlsCert.get()) {
            debug("using TLS client certificate %s for '%s'", PathFmt(*cert), request.uri);
            request.tlsCert = *cert;
        }

        if (const auto & key = config->tlsKey.get()) {
            debug("using TLS client key '%s' for '%s'", PathFmt(*key), request.uri);
            request.tlsKey = *key;
        }
    }

    return request;
}

void HttpBinaryCacheStore::getFile(const std::string & path, Sink & sink)
{
    checkEnabled();
    auto request(makeRequest(path));
    try {
        fileTransfer->download(std::move(request), sink);
    } catch (FileTransferError & e) {
        if (e.error == FileTransfer::NotFound || e.error == FileTransfer::Forbidden)
            throw NoSuchBinaryCacheFile(
                "file '%s' does not exist in binary cache '%s'", path, config->getHumanReadableURI());
        maybeDisable();
        throw;
    }
}

void HttpBinaryCacheStore::getFile(const std::string & path, Callback<std::optional<std::string>> callback) noexcept
{
    auto callbackPtr = std::make_shared<decltype(callback)>(std::move(callback));

    try {
        checkEnabled();

        auto request(makeRequest(path));

        fileTransfer->enqueueFileTransfer(request, {[callbackPtr, this](std::future<FileTransferResult> result) {
                                              try {
                                                  (*callbackPtr)(std::move(result.get().data));
                                              } catch (FileTransferError & e) {
                                                  if (e.error == FileTransfer::NotFound
                                                      || e.error == FileTransfer::Forbidden)
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

std::optional<std::string> HttpBinaryCacheStore::getNixCacheInfo()
{
    try {
        auto result = fileTransfer->download(makeRequest(cacheInfoFile));
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
std::optional<TrustedFlag> HttpBinaryCacheStore::isTrustedClient()
{
    return std::nullopt;
}

ref<Store> HttpBinaryCacheStore::Config::openStore(ref<FileTransfer> fileTransfer) const
{
    return make_ref<HttpBinaryCacheStore>(
        ref{// FIXME we shouldn't actually need a mutable config
            std::const_pointer_cast<HttpBinaryCacheStore::Config>(shared_from_this())},
        fileTransfer);
}

ref<Store> HttpBinaryCacheStoreConfig::openStore() const
{
    return openStore(getFileTransfer());
}

static RegisterStoreImplementation<HttpBinaryCacheStore::Config> regHttpBinaryCacheStore;

} // namespace nix
