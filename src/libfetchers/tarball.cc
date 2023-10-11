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
#include "fs-input-accessor.hh"
#include "store-api.hh"
#include "git-utils.hh"

namespace nix::fetchers {

DownloadFileResult downloadFile(
    ref<Store> store,
    const std::string & url,
    const std::string & name,
    bool locked,
    const Headers & headers)
{
    // FIXME: check store

    Attrs inAttrs({
        {"type", "file"},
        {"url", url},
        {"name", name},
    });

    auto cached = getCache()->lookupExpired(store, inAttrs);

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

    // FIXME: write to temporary file.
    Attrs infoAttrs({
        {"etag", res.etag},
        {"url", res.effectiveUri},
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
        auto hash = hashString(htSHA256, res.data);
        ValidPathInfo info {
            *store,
            name,
            FixedOutputInfo {
                .method = FileIngestionMethod::Flat,
                .hash = hash,
                .references = {},
            },
            hashString(htSHA256, sink.s),
        };
        info.narSize = sink.s.size();
        auto source = StringSource { sink.s };
        store->addToStore(info, source, NoRepair, NoCheckSigs);
        storePath = std::move(info.path);
    }

    getCache()->add(
        store,
        inAttrs,
        infoAttrs,
        *storePath,
        locked);

    if (url != res.effectiveUri)
        getCache()->add(
            store,
            {
                {"type", "file"},
                {"url", res.effectiveUri},
                {"name", name},
            },
            infoAttrs,
            *storePath,
            locked);

    return {
        .storePath = std::move(*storePath),
        .etag = res.etag,
        .effectiveUrl = res.effectiveUri,
        .immutableUrl = res.immutableUrl,
    };
}

DownloadTarballResult downloadTarball(
    ref<Store> store,
    const std::string & url,
    const std::string & name,
    bool locked,
    const Headers & headers)
{
    Attrs inAttrs({
        {"type", "tarball"},
        {"url", url},
        {"name", name},
    });

    auto cached = getCache()->lookupExpired(store, inAttrs);

    if (cached && !cached->expired)
        return {
            .storePath = std::move(cached->storePath),
            .lastModified = (time_t) getIntAttr(cached->infoAttrs, "lastModified"),
            .immutableUrl = maybeGetStrAttr(cached->infoAttrs, "immutableUrl"),
        };

    auto res = downloadFile(store, url, name, locked, headers);

    std::optional<StorePath> unpackedStorePath;
    time_t lastModified;

    if (cached && res.etag != "" && getStrAttr(cached->infoAttrs, "etag") == res.etag) {
        unpackedStorePath = std::move(cached->storePath);
        lastModified = getIntAttr(cached->infoAttrs, "lastModified");
    } else {
        Path tmpDir = createTempDir();
        AutoDelete autoDelete(tmpDir, true);
        unpackTarfile(store->toRealPath(res.storePath), tmpDir);
        auto members = readDirectory(tmpDir);
        if (members.size() != 1)
            throw nix::Error("tarball '%s' contains an unexpected number of top-level files", url);
        auto topDir = tmpDir + "/" + members.begin()->name;
        lastModified = lstat(topDir).st_mtime;
        unpackedStorePath = store->addToStore(name, topDir, FileIngestionMethod::Recursive, htSHA256, defaultPathFilter, NoRepair);
    }

    Attrs infoAttrs({
        {"lastModified", uint64_t(lastModified)},
        {"etag", res.etag},
    });

    if (res.immutableUrl)
        infoAttrs.emplace("immutableUrl", *res.immutableUrl);

    getCache()->add(
        store,
        inAttrs,
        infoAttrs,
        *unpackedStorePath,
        locked);

    return {
        .storePath = std::move(*unpackedStorePath),
        .lastModified = lastModified,
        .immutableUrl = res.immutableUrl,
    };
}

struct DownloadTarballResult2
{
    Hash treeHash;
    time_t lastModified;
    std::optional<std::string> immutableUrl;
};

/* Download and import a tarball into the Git cache. The result is
   the Git tree hash of the root directory. */
DownloadTarballResult2 downloadTarball2(
    ref<Store> store,
    const std::string & url,
    const Headers & headers)
{
    Attrs inAttrs({
        {"_what", "tarballCache"},
        {"url", url},
    });

    auto cached = getCache()->lookupExpired2(inAttrs);

    auto attrsToResult = [&](const Attrs & infoAttrs)
    {
        return DownloadTarballResult2 {
            .treeHash = getRev(infoAttrs, "treeHash"),
            .lastModified = (time_t) getIntAttr(infoAttrs, "lastModified"),
            .immutableUrl = maybeGetStrAttr(infoAttrs, "immutableUrl"),
        };
    };

    if (cached && !getTarballCache()->hasObject(getRev(cached->infoAttrs, "treeHash")))
        cached.reset();

    if (cached && !cached->expired)
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

    /* Note: if the download is cached, `importTarball()` will receive
       no data, which causes it to import an empty tarball. */
    auto tarballInfo = getTarballCache()->importTarball(*source);

    auto res(_res->lock());

    Attrs infoAttrs;

    if (res->cached) {
        infoAttrs = cached->infoAttrs;
    } else {
        infoAttrs.insert_or_assign("etag", res->etag);
        infoAttrs.insert_or_assign("treeHash", tarballInfo.treeHash.gitRev());
        infoAttrs.insert_or_assign("lastModified", uint64_t(tarballInfo.lastModified));
        if (res->immutableUrl)
            infoAttrs.insert_or_assign("immutableUrl", *res->immutableUrl);
    }

    getCache()->add(inAttrs, infoAttrs);

    if (url != res->effectiveUri) {
        Attrs inAttrs2(inAttrs);
        inAttrs2.insert_or_assign("url", res->effectiveUri);
        getCache()->add(inAttrs2, infoAttrs);
    }

    // FIXME: add a cache entry for immutableUrl? That could allow
    // cache poisoning.

    return attrsToResult(infoAttrs);
}

// An input scheme corresponding to a curl-downloadable resource.
struct CurlInputScheme : InputScheme
{
    virtual const std::string inputType() const = 0;
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

        for (auto & param : specialParams)
            url.query.erase(param);

        input.attrs.insert_or_assign("type", inputType());
        input.attrs.insert_or_assign("url", url.to_string());
        return input;
    }

    std::optional<Input> inputFromAttrs(const Attrs & attrs) const override
    {
        auto type = maybeGetStrAttr(attrs, "type");
        if (type != inputType()) return {};

        // FIXME: some of these only apply to TarballInputScheme.
        for (auto & [name, value] : attrs)
            if (!specialParams.count(name))
                throw Error("unsupported %s input attribute '%s'", *type, name);

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
            url.query.insert_or_assign("narHash", narHash->to_string(SRI, true));
        return url;
    }

    bool isLocked(const Input & input) const override
    {
        return (bool) input.getNarHash();
    }
};

const std::set<std::string> CurlInputScheme::specialParams{
    "type", "url", "narHash", "name", "unpack", "rev", "revCount", "lastModified"
};

struct FileInputScheme : CurlInputScheme
{
    const std::string inputType() const override { return "file"; }

    bool isValidURL(const ParsedURL & url, bool requireTree) const override
    {
        auto parsedUrlScheme = parseUrlScheme(url.scheme);
        return transportUrlSchemes.count(std::string(parsedUrlScheme.transport))
            && (parsedUrlScheme.application
                ? parsedUrlScheme.application.value() == inputType()
                : (!requireTree && !hasTarballExtension(url.path)));
    }

    std::pair<ref<InputAccessor>, Input> getAccessor(ref<Store> store, const Input & _input) const override
    {
        auto input(_input);

        auto file = downloadFile(store, getStrAttr(input.attrs, "url"), input.getName(), false);

        // FIXME: remove?
        auto narHash = store->queryPathInfo(file.storePath)->narHash;
        input.attrs.insert_or_assign("narHash", narHash.to_string(SRI, true));

        return {makeStorePathAccessor(store, file.storePath), input};
    }
};

struct TarballInputScheme : CurlInputScheme
{
    const std::string inputType() const override { return "tarball"; }

    bool isValidURL(const ParsedURL & url, bool requireTree) const override
    {
        auto parsedUrlScheme = parseUrlScheme(url.scheme);

        return transportUrlSchemes.count(std::string(parsedUrlScheme.transport))
            && (parsedUrlScheme.application
                ? parsedUrlScheme.application.value() == inputType()
                : (requireTree || hasTarballExtension(url.path)));
    }

    std::pair<ref<InputAccessor>, Input> getAccessor(ref<Store> store, const Input & _input) const override
    {
        auto input(_input);

        auto result = downloadTarball2(store, getStrAttr(input.attrs, "url"), {});

        auto accessor = getTarballCache()->getAccessor(result.treeHash);

        accessor->setPathDisplay("«" + input.to_string() + "»");

        if (result.lastModified && !input.attrs.contains("lastModified"))
            input.attrs.insert_or_assign("lastModified", uint64_t(result.lastModified));

        if (result.immutableUrl) {
            auto immutableInput = Input::fromURL(*result.immutableUrl);
            // FIXME: would be nice to support arbitrary flakerefs
            // here, e.g. git flakes.
            if (immutableInput.getType() != "tarball")
                throw Error("tarball 'Link' headers that redirect to non-tarball URLs are not supported");
            input = immutableInput;
        }

        input.attrs.insert_or_assign("narHash",
            getTarballCache()->treeHashToNarHash(result.treeHash).to_string(SRI, true));

        return {accessor, input};
    }
};

static auto rTarballInputScheme = OnStartup([] { registerInputScheme(std::make_unique<TarballInputScheme>()); });
static auto rFileInputScheme = OnStartup([] { registerInputScheme(std::make_unique<FileInputScheme>()); });

}
