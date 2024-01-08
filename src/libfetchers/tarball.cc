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

    getCache()->add(
        *store,
        inAttrs,
        infoAttrs,
        *storePath,
        locked);

    if (url != res.effectiveUri)
        getCache()->add(
            *store,
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

    auto cached = getCache()->lookupExpired(*store, inAttrs);

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
        PosixSourceAccessor accessor;
        unpackedStorePath = store->addToStore(name, accessor, CanonPath { topDir }, FileIngestionMethod::Recursive, HashAlgorithm::SHA256, {}, defaultPathFilter, NoRepair);
    }

    Attrs infoAttrs({
        {"lastModified", uint64_t(lastModified)},
        {"etag", res.etag},
    });

    if (res.immutableUrl)
        infoAttrs.emplace("immutableUrl", *res.immutableUrl);

    getCache()->add(
        *store,
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

        url.query.erase("rev");
        url.query.erase("revCount");

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

    std::pair<StorePath, Input> fetch(ref<Store> store, const Input & input) override
    {
        auto file = downloadFile(store, getStrAttr(input.attrs, "url"), input.getName(), false);
        return {std::move(file.storePath), input};
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

    std::pair<StorePath, Input> fetch(ref<Store> store, const Input & _input) override
    {
        Input input(_input);
        auto url = getStrAttr(input.attrs, "url");
        auto result = downloadTarball(store, url, input.getName(), false);

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

        return {result.storePath, std::move(input)};
    }
};

static auto rTarballInputScheme = OnStartup([] { registerInputScheme(std::make_unique<TarballInputScheme>()); });
static auto rFileInputScheme = OnStartup([] { registerInputScheme(std::make_unique<FileInputScheme>()); });

}
