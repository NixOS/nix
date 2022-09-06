#include "fetchers.hh"
#include "cache.hh"
#include "filetransfer.hh"
#include "globals.hh"
#include "store-api.hh"
#include "archive.hh"
#include "tarfile.hh"
#include "types.hh"
#include "split.hh"

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
            .effectiveUrl = getStrAttr(cached->infoAttrs, "url")
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

    std::optional<StorePath> storePath;

    if (res.cached) {
        assert(cached);
        storePath = std::move(cached->storePath);
    } else {
        StringSink sink;
        dumpString(res.data, sink);
        auto hash = hashString(htSHA256, res.data);
        ValidPathInfo info {
            store->makeFixedOutputPath(FileIngestionMethod::Flat, hash, name),
            hashString(htSHA256, sink.s),
        };
        info.narSize = sink.s.size();
        info.ca = FixedOutputHash {
            .method = FileIngestionMethod::Flat,
            .hash = hash,
        };
        auto source = StringSource(sink.s);
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
    };
}

std::pair<Tree, time_t> downloadTarball(
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
            Tree { .actualPath = store->toRealPath(cached->storePath), .storePath = std::move(cached->storePath) },
            getIntAttr(cached->infoAttrs, "lastModified")
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

    getCache()->add(
        store,
        inAttrs,
        infoAttrs,
        *unpackedStorePath,
        locked);

    return {
        Tree { .actualPath = store->toRealPath(*unpackedStorePath), .storePath = std::move(*unpackedStorePath) },
        lastModified,
    };
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

    virtual bool isValidURL(const ParsedURL & url) const = 0;

    std::optional<Input> inputFromURL(const ParsedURL & url) override
    {
        if (!isValidURL(url))
            return std::nullopt;

        Input input;

        auto urlWithoutApplicationScheme = url;
        urlWithoutApplicationScheme.scheme = parseUrlScheme(url.scheme).transport;

        input.attrs.insert_or_assign("type", inputType());
        input.attrs.insert_or_assign("url", urlWithoutApplicationScheme.to_string());
        auto narHash = url.query.find("narHash");
        if (narHash != url.query.end())
            input.attrs.insert_or_assign("narHash", narHash->second);
        return input;
    }

    std::optional<Input> inputFromAttrs(const Attrs & attrs) override
    {
        auto type = maybeGetStrAttr(attrs, "type");
        if (type != inputType()) return {};

        std::set<std::string> allowedNames = {"type", "url", "narHash", "name", "unpack"};
        for (auto & [name, value] : attrs)
            if (!allowedNames.count(name))
                throw Error("unsupported %s input attribute '%s'", *type, name);

        Input input;
        input.attrs = attrs;

        //input.locked = (bool) maybeGetStrAttr(input.attrs, "hash");
        return input;
    }

    ParsedURL toURL(const Input & input) override
    {
        auto url = parseURL(getStrAttr(input.attrs, "url"));
        // NAR hashes are preferred over file hashes since tar/zip files        // don't have a canonical representation.
        if (auto narHash = input.getNarHash())
            url.query.insert_or_assign("narHash", narHash->to_string(SRI, true));
        return url;
    }

    bool hasAllInfo(const Input & input) override
    {
        return true;
    }

};

struct FileInputScheme : CurlInputScheme
{
    const std::string inputType() const override { return "file"; }

    bool isValidURL(const ParsedURL & url) const override
    {
        auto parsedUrlScheme = parseUrlScheme(url.scheme);
        return transportUrlSchemes.count(std::string(parsedUrlScheme.transport))
            && (parsedUrlScheme.application
                    ? parsedUrlScheme.application.value() == inputType()
                    : !hasTarballExtension(url.path));
    }

    std::pair<StorePath, Input> fetch(ref<Store> store, const Input & input) override
    {
        auto file = downloadFile(store, getStrAttr(input.attrs, "url"), input.getName(), false);
        return {std::move(file.storePath), input};
    }
};

struct TarballInputScheme : CurlInputScheme
{
    const std::string inputType() const override { return "tarball"; }

    bool isValidURL(const ParsedURL & url) const override
    {
        auto parsedUrlScheme = parseUrlScheme(url.scheme);

        return transportUrlSchemes.count(std::string(parsedUrlScheme.transport))
            && (parsedUrlScheme.application
                    ? parsedUrlScheme.application.value() == inputType()
                    : hasTarballExtension(url.path));
    }

    std::pair<StorePath, Input> fetch(ref<Store> store, const Input & input) override
    {
        auto tree = downloadTarball(store, getStrAttr(input.attrs, "url"), input.getName(), false).first;
        return {std::move(tree.storePath), input};
    }
};

static auto rTarballInputScheme = OnStartup([] { registerInputScheme(std::make_unique<TarballInputScheme>()); });
static auto rFileInputScheme = OnStartup([] { registerInputScheme(std::make_unique<FileInputScheme>()); });

}
