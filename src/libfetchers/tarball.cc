#include "tarball.hh"
#include "fetchers.hh"
#include "cache.hh"
#include "filetransfer.hh"
#include "globals.hh"
#include "store-api.hh"
#include "archive.hh"
#include "tarfile.hh"
#include "types.hh"
#include "split.hh"
#include "posix-source-accessor.hh"
#include "fs-input-accessor.hh"
#include "store-api.hh"
#include "git-utils.hh"

namespace nix::fetchers {

DownloadFileResult downloadFile(
    ref<Store> store,
    const std::string & url,
    const std::string & name,
    const Headers & headers)
{
    // FIXME: check store

    Attrs inAttrs({
        {"type", "file"},
        {"url", url},
        {"name", name},
    });

    auto cached = getCache()->lookupExpired(*store, inAttrs);

    auto useCached = [&]() -> DownloadFileResult
    {
        return {
            .storePath = std::move(cached->storePath),
            .etag = getStrAttr(cached->infoAttrs, "etag"),
            .effectiveUrl = getStrAttr(cached->infoAttrs, "url"),
            .immutableUrl = maybeGetStrAttr(cached->infoAttrs, "immutableUrl"),
        };
    };

    if (cached && !cached->expired)
        return useCached();

    FileTransferRequest request(url);
    request.headers = headers;
    if (cached)
        request.expectedETag = getStrAttr(cached->infoAttrs, "etag");
    FileTransferResult res;
    try {
        res = getFileTransfer()->download(request);
    } catch (FileTransferError & e) {
        if (cached) {
            warn("%s; using cached version", e.msg());
            return useCached();
        } else
            throw;
    }

    Attrs infoAttrs({
        {"etag", res.etag},
    });

    if (res.immutableUrl)
        infoAttrs.emplace("immutableUrl", *res.immutableUrl);

    std::optional<StorePath> storePath;

    if (res.cached) {
        assert(cached);
        storePath = std::move(cached->storePath);
    } else {
        StringSink sink;
        dumpString(res.data, sink);
        auto hash = hashString(HashAlgorithm::SHA256, res.data);
        ValidPathInfo info {
            *store,
            name,
            FixedOutputInfo {
                .method = FileIngestionMethod::Flat,
                .hash = hash,
                .references = {},
            },
            hashString(HashAlgorithm::SHA256, sink.s),
        };
        info.narSize = sink.s.size();
        auto source = StringSource { sink.s };
        store->addToStore(info, source, NoRepair, NoCheckSigs);
        storePath = std::move(info.path);
    }

    /* Cache metadata for all URLs in the redirect chain. */
    for (auto & url : res.urls) {
        inAttrs.insert_or_assign("url", url);
        infoAttrs.insert_or_assign("url", *res.urls.rbegin());
        getCache()->add(
            *store,
            inAttrs,
            infoAttrs,
            *storePath,
            false);
    }

    return {
        .storePath = std::move(*storePath),
        .etag = res.etag,
        .effectiveUrl = *res.urls.rbegin(),
        .immutableUrl = res.immutableUrl,
    };
}

DownloadTarballResult downloadTarball(
    const std::string & url,
    const Headers & headers)
{
    Attrs inAttrs({
        {"_what", "tarballCache"},
        {"url", url},
    });

    auto cached = getCache()->lookupExpired(inAttrs);

    auto attrsToResult = [&](const Attrs & infoAttrs)
    {
        auto treeHash = getRevAttr(infoAttrs, "treeHash");
        return DownloadTarballResult {
            .treeHash = treeHash,
            .lastModified = (time_t) getIntAttr(infoAttrs, "lastModified"),
            .immutableUrl = maybeGetStrAttr(infoAttrs, "immutableUrl"),
            .accessor = getTarballCache()->getAccessor(treeHash, false),
        };
    };

    if (cached && !getTarballCache()->hasObject(getRevAttr(cached->infoAttrs, "treeHash")))
        cached.reset();

    if (cached && !cached->expired)
        /* We previously downloaded this tarball and it's younger than
           `tarballTtl`, so no need to check the server. */
        return attrsToResult(cached->infoAttrs);

    auto _res = std::make_shared<Sync<FileTransferResult>>();

    auto source = sinkToSource([&](Sink & sink) {
        FileTransferRequest req(url);
        req.expectedETag = cached ? getStrAttr(cached->infoAttrs, "etag") : "";
        getFileTransfer()->download(std::move(req), sink,
            [_res](FileTransferResult r)
            {
                *_res->lock() = r;
            });
    });

    // TODO: fall back to cached value if download fails.

    /* Note: if the download is cached, `importTarball()` will receive
       no data, which causes it to import an empty tarball. */
    TarArchive archive { *source };
    auto parseSink = getTarballCache()->getFileSystemObjectSink();
    auto lastModified = unpackTarfileToSink(archive, *parseSink);

    auto res(_res->lock());

    Attrs infoAttrs;

    if (res->cached) {
        /* The server says that the previously downloaded version is
           still current. */
        infoAttrs = cached->infoAttrs;
    } else {
        infoAttrs.insert_or_assign("etag", res->etag);
        infoAttrs.insert_or_assign("treeHash", parseSink->sync().gitRev());
        infoAttrs.insert_or_assign("lastModified", uint64_t(lastModified));
        if (res->immutableUrl)
            infoAttrs.insert_or_assign("immutableUrl", *res->immutableUrl);
    }

    /* Insert a cache entry for every URL in the redirect chain. */
    for (auto & url : res->urls) {
        inAttrs.insert_or_assign("url", url);
        getCache()->upsert(inAttrs, infoAttrs);
    }

    // FIXME: add a cache entry for immutableUrl? That could allow
    // cache poisoning.

    return attrsToResult(infoAttrs);
}

// An input scheme corresponding to a curl-downloadable resource.
struct CurlInputScheme : InputScheme
{
    const std::set<std::string> transportUrlSchemes = {"file", "http", "https"};

    const bool hasTarballExtension(std::string_view path) const
    {
        return hasSuffix(path, ".zip") || hasSuffix(path, ".tar")
            || hasSuffix(path, ".tgz") || hasSuffix(path, ".tar.gz")
            || hasSuffix(path, ".tar.xz") || hasSuffix(path, ".tar.bz2")
            || hasSuffix(path, ".tar.zst");
    }

    virtual bool isValidURL(const ParsedURL & url, bool requireTree) const = 0;

    static const std::set<std::string> specialParams;

    std::optional<Input> inputFromURL(const ParsedURL & _url, bool requireTree) const override
    {
        if (!isValidURL(_url, requireTree))
            return std::nullopt;

        Input input;

        auto url = _url;

        url.scheme = parseUrlScheme(url.scheme).transport;

        auto narHash = url.query.find("narHash");
        if (narHash != url.query.end())
            input.attrs.insert_or_assign("narHash", narHash->second);

        if (auto i = get(url.query, "rev"))
            input.attrs.insert_or_assign("rev", *i);

        if (auto i = get(url.query, "revCount"))
            if (auto n = string2Int<uint64_t>(*i))
                input.attrs.insert_or_assign("revCount", *n);

        if (auto i = get(url.query, "lastModified"))
            if (auto n = string2Int<uint64_t>(*i))
                input.attrs.insert_or_assign("lastModified", *n);

        /* The URL query parameters serve two roles: specifying fetch
           settings for Nix itself, and arbitrary data as part of the
           HTTP request. Now that we've processed the Nix-specific
           attributes above, remove them so we don't also send them as
           part of the HTTP request. */
        for (auto & param : allowedAttrs())
            url.query.erase(param);

        input.attrs.insert_or_assign("type", std::string { schemeName() });
        input.attrs.insert_or_assign("url", url.to_string());
        return input;
    }

    StringSet allowedAttrs() const override
    {
        return {
            "type",
            "url",
            "narHash",
            "name",
            "unpack",
            "rev",
            "revCount",
            "lastModified",
        };
    }

    std::optional<Input> inputFromAttrs(const Attrs & attrs) const override
    {
        Input input;
        input.attrs = attrs;

        //input.locked = (bool) maybeGetStrAttr(input.attrs, "hash");
        return input;
    }

    ParsedURL toURL(const Input & input) const override
    {
        auto url = parseURL(getStrAttr(input.attrs, "url"));
        // NAR hashes are preferred over file hashes since tar/zip
        // files don't have a canonical representation.
        if (auto narHash = input.getNarHash())
            url.query.insert_or_assign("narHash", narHash->to_string(HashFormat::SRI, true));
        return url;
    }

    bool isLocked(const Input & input) const override
    {
        return (bool) input.getNarHash();
    }
};

struct FileInputScheme : CurlInputScheme
{
    std::string_view schemeName() const override { return "file"; }

    bool isValidURL(const ParsedURL & url, bool requireTree) const override
    {
        auto parsedUrlScheme = parseUrlScheme(url.scheme);
        return transportUrlSchemes.count(std::string(parsedUrlScheme.transport))
            && (parsedUrlScheme.application
                ? parsedUrlScheme.application.value() == schemeName()
                : (!requireTree && !hasTarballExtension(url.path)));
    }

    std::pair<ref<InputAccessor>, Input> getAccessor(ref<Store> store, const Input & _input) const override
    {
        auto input(_input);

        /* Unlike TarballInputScheme, this stores downloaded files in
           the Nix store directly, since there is little deduplication
           benefit in using the Git cache for single big files like
           tarballs. */
        auto file = downloadFile(store, getStrAttr(input.attrs, "url"), input.getName());

        auto narHash = store->queryPathInfo(file.storePath)->narHash;
        input.attrs.insert_or_assign("narHash", narHash.to_string(HashFormat::SRI, true));

        auto accessor = makeStorePathAccessor(store, file.storePath);

        accessor->setPathDisplay("«" + input.to_string() + "»");

        return {accessor, input};
    }
};

struct TarballInputScheme : CurlInputScheme
{
    std::string_view schemeName() const override { return "tarball"; }

    bool isValidURL(const ParsedURL & url, bool requireTree) const override
    {
        auto parsedUrlScheme = parseUrlScheme(url.scheme);

        return transportUrlSchemes.count(std::string(parsedUrlScheme.transport))
            && (parsedUrlScheme.application
                ? parsedUrlScheme.application.value() == schemeName()
                : (requireTree || hasTarballExtension(url.path)));
    }

    std::pair<ref<InputAccessor>, Input> getAccessor(ref<Store> store, const Input & _input) const override
    {
        auto input(_input);

        auto result = downloadTarball(getStrAttr(input.attrs, "url"), {});

        result.accessor->setPathDisplay("«" + input.to_string() + "»");

        if (result.immutableUrl) {
            auto immutableInput = Input::fromURL(*result.immutableUrl);
            // FIXME: would be nice to support arbitrary flakerefs
            // here, e.g. git flakes.
            if (immutableInput.getType() != "tarball")
                throw Error("tarball 'Link' headers that redirect to non-tarball URLs are not supported");
            input = immutableInput;
        }

        if (result.lastModified && !input.attrs.contains("lastModified"))
            input.attrs.insert_or_assign("lastModified", uint64_t(result.lastModified));

        input.attrs.insert_or_assign("narHash",
            getTarballCache()->treeHashToNarHash(result.treeHash).to_string(HashFormat::SRI, true));

        return {result.accessor, input};
    }
};

static auto rTarballInputScheme = OnStartup([] { registerInputScheme(std::make_unique<TarballInputScheme>()); });
static auto rFileInputScheme = OnStartup([] { registerInputScheme(std::make_unique<FileInputScheme>()); });

}
