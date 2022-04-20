#include "fetchers.hh"
#include "cache.hh"
#include "filetransfer.hh"
#include "globals.hh"
#include "store-api.hh"
#include "archive.hh"
#include "tarfile.hh"
#include "types.hh"

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

    std::optional<StorePathDescriptor> storePath;

    if (res.cached) {
        assert(cached);
        storePath = std::move(cached->storePath);
    } else {
        StringSink sink;
        dumpString(res.data, sink);
        auto hash = hashString(htSHA256, res.data);
        storePath = {
            .name = name,
            .info = FixedOutputInfo {
                {
                    .method = FileIngestionMethod::Flat,
                    .hash = hash,
                },
                {},
            },
        };
        ValidPathInfo info {
            *store,
            StorePathDescriptor { *storePath },
            hashString(htSHA256, sink.s),
        };
        info.narSize = sink.s.size();
        auto source = StringSource { sink.s };
        store->addToStore(info, source, NoRepair, NoCheckSigs);
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
            Tree {
                .actualPath = store->toRealPath(store->makeFixedOutputPathFromCA(cached->storePath)),
                .storePath = std::move(cached->storePath),
            },
            getIntAttr(cached->infoAttrs, "lastModified")
        };

    auto res = downloadFile(store, url, name, locked, headers);

    std::optional<StorePathDescriptor> unpackedStorePath;
    time_t lastModified;

    if (cached && res.etag != "" && getStrAttr(cached->infoAttrs, "etag") == res.etag) {
        unpackedStorePath = std::move(cached->storePath);
        lastModified = getIntAttr(cached->infoAttrs, "lastModified");
    } else {
        Path tmpDir = createTempDir();
        AutoDelete autoDelete(tmpDir, true);
        unpackTarfile(
            store->toRealPath(store->makeFixedOutputPathFromCA(res.storePath)),
            tmpDir);
        auto members = readDirectory(tmpDir);
        if (members.size() != 1)
            throw nix::Error("tarball '%s' contains an unexpected number of top-level files", url);
        auto topDir = tmpDir + "/" + members.begin()->name;
        lastModified = lstat(topDir).st_mtime;
        auto temp = store->addToStore(name, topDir, FileIngestionMethod::Recursive, htSHA256, defaultPathFilter, NoRepair);
        // FIXME: just have Store::addToStore return a StorePathDescriptor, as
        // it has the underlying information.
        unpackedStorePath = store->queryPathInfo(temp)->fullStorePathDescriptorOpt().value();
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
        Tree {
            .actualPath = store->toRealPath(store->makeFixedOutputPathFromCA(*unpackedStorePath)),
            .storePath = std::move(*unpackedStorePath)
        },
        lastModified,
    };
}

struct TarballInputScheme : InputScheme
{
    std::optional<Input> inputFromURL(const ParsedURL & url) override
    {
        if (url.scheme != "file" && url.scheme != "http" && url.scheme != "https") return {};

        if (!hasSuffix(url.path, ".zip")
            && !hasSuffix(url.path, ".tar")
            && !hasSuffix(url.path, ".tgz")
            && !hasSuffix(url.path, ".tar.gz")
            && !hasSuffix(url.path, ".tar.xz")
            && !hasSuffix(url.path, ".tar.bz2")
            && !hasSuffix(url.path, ".tar.zst"))
            return {};

        Input input;
        input.attrs.insert_or_assign("type", "tarball");
        input.attrs.insert_or_assign("url", url.to_string());
        auto narHash = url.query.find("narHash");
        if (narHash != url.query.end())
            input.attrs.insert_or_assign("narHash", narHash->second);
        return input;
    }

    std::optional<Input> inputFromAttrs(const Attrs & attrs) override
    {
        if (maybeGetStrAttr(attrs, "type") != "tarball") return {};

        for (auto & [name, value] : attrs)
            if (name != "type" && name != "url" && /* name != "hash" && */ name != "narHash" && name != "name")
                throw Error("unsupported tarball input attribute '%s'", name);

        Input input;
        input.attrs = attrs;
        //input.locked = (bool) maybeGetStrAttr(input.attrs, "hash");
        return input;
    }

    ParsedURL toURL(const Input & input) override
    {
        auto url = parseURL(getStrAttr(input.attrs, "url"));
        // NAR hashes are preferred over file hashes since tar/zip files
        // don't have a canonical representation.
        if (auto narHash = input.getNarHash())
            url.query.insert_or_assign("narHash", narHash->to_string(SRI, true));
        /*
        else if (auto hash = maybeGetStrAttr(input.attrs, "hash"))
            url.query.insert_or_assign("hash", Hash(*hash).to_string(SRI, true));
        */
        return url;
    }

    bool hasAllInfo(const Input & input) override
    {
        return true;
    }

    std::pair<StorePathDescriptor, Input> fetch(ref<Store> store, const Input & input) override
    {
        auto tree = downloadTarball(store, getStrAttr(input.attrs, "url"), input.getName(), false).first;
        return {std::move(tree.storePath), input};
    }
};

static auto rTarballInputScheme = OnStartup([] { registerInputScheme(std::make_unique<TarballInputScheme>()); });

}
