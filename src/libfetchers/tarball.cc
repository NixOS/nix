#include "fetchers.hh"
#include "cache.hh"
#include "filetransfer.hh"
#include "globals.hh"
#include "store-api.hh"
#include "archive.hh"
#include "tarfile.hh"

namespace nix::fetchers {

DownloadFileResult downloadFile(
    ref<Store> store,
    const std::string & url,
    const std::string & name,
    bool immutable)
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
        assert(request.expectedETag == res.etag);
        storePath = std::move(cached->storePath);
    } else {
        StringSink sink;
        dumpString(*res.data, sink);
        auto hash = hashString(htSHA256, *res.data);
        ValidPathInfo info(store->makeFixedOutputPath(FileIngestionMethod::Flat, hash, name));
        info.narHash = hashString(htSHA256, *sink.s);
        info.narSize = sink.s->size();
        info.ca = makeFixedOutputCA(FileIngestionMethod::Flat, hash);
        auto source = StringSource { *sink.s };
        store->addToStore(info, source, NoRepair, NoCheckSigs);
        storePath = std::move(info.path);
    }

    getCache()->add(
        store,
        inAttrs,
        infoAttrs,
        *storePath,
        immutable);

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
            immutable);

    return {
        .storePath = std::move(*storePath),
        .etag = res.etag,
        .effectiveUrl = res.effectiveUri,
    };
}

Tree downloadTarball(
    ref<Store> store,
    const std::string & url,
    const std::string & name,
    bool immutable)
{
    Attrs inAttrs({
        {"type", "tarball"},
        {"url", url},
        {"name", name},
    });

    auto cached = getCache()->lookupExpired(store, inAttrs);

    if (cached && !cached->expired)
        return Tree {
            .actualPath = store->toRealPath(cached->storePath),
            .storePath = std::move(cached->storePath),
            .info = TreeInfo {
                .lastModified = getIntAttr(cached->infoAttrs, "lastModified"),
            },
        };

    auto res = downloadFile(store, url, name, immutable);

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
        {"lastModified", lastModified},
        {"etag", res.etag},
    });

    getCache()->add(
        store,
        inAttrs,
        infoAttrs,
        *unpackedStorePath,
        immutable);

    return Tree {
        .actualPath = store->toRealPath(*unpackedStorePath),
        .storePath = std::move(*unpackedStorePath),
        .info = TreeInfo {
            .lastModified = lastModified,
        },
    };
}

struct TarballInput : Input
{
    ParsedURL url;
    std::optional<Hash> hash;

    TarballInput(const ParsedURL & url) : url(url)
    { }

    std::string type() const override { return "tarball"; }

    bool operator ==(const Input & other) const override
    {
        auto other2 = dynamic_cast<const TarballInput *>(&other);
        return
            other2
            && to_string() == other2->to_string()
            && hash == other2->hash;
    }

    bool isImmutable() const override
    {
        return hash || narHash;
    }

    ParsedURL toURL() const override
    {
        auto url2(url);
        // NAR hashes are preferred over file hashes since tar/zip files
        // don't have a canonical representation.
        if (narHash)
            url2.query.insert_or_assign("narHash", narHash->to_string(SRI, true));
        else if (hash)
            url2.query.insert_or_assign("hash", hash->to_string(SRI, true));
        return url2;
    }

    Attrs toAttrsInternal() const override
    {
        Attrs attrs;
        attrs.emplace("url", url.to_string());
        if (hash)
            attrs.emplace("hash", hash->to_string(SRI, true));
        return attrs;
    }

    std::pair<Tree, std::shared_ptr<const Input>> fetchTreeInternal(nix::ref<Store> store) const override
    {
        auto tree = downloadTarball(store, url.to_string(), "source", false);

        auto input = std::make_shared<TarballInput>(*this);
        input->narHash = store->queryPathInfo(tree.storePath)->narHash;

        return {std::move(tree), input};
    }
};

struct TarballInputScheme : InputScheme
{
    std::unique_ptr<Input> inputFromURL(const ParsedURL & url) override
    {
        if (url.scheme != "file" && url.scheme != "http" && url.scheme != "https") return nullptr;

        if (!hasSuffix(url.path, ".zip")
            && !hasSuffix(url.path, ".tar")
            && !hasSuffix(url.path, ".tar.gz")
            && !hasSuffix(url.path, ".tar.xz")
            && !hasSuffix(url.path, ".tar.bz2"))
            return nullptr;

        auto input = std::make_unique<TarballInput>(url);

        auto hash = input->url.query.find("hash");
        if (hash != input->url.query.end()) {
            // FIXME: require SRI hash.
            input->hash = Hash(hash->second);
            input->url.query.erase(hash);
        }

        auto narHash = input->url.query.find("narHash");
        if (narHash != input->url.query.end()) {
            // FIXME: require SRI hash.
            input->narHash = Hash(narHash->second);
            input->url.query.erase(narHash);
        }

        return input;
    }

    std::unique_ptr<Input> inputFromAttrs(const Attrs & attrs) override
    {
        if (maybeGetStrAttr(attrs, "type") != "tarball") return {};

        for (auto & [name, value] : attrs)
            if (name != "type" && name != "url" && name != "hash")
                throw Error("unsupported tarball input attribute '%s'", name);

        auto input = std::make_unique<TarballInput>(parseURL(getStrAttr(attrs, "url")));
        if (auto hash = maybeGetStrAttr(attrs, "hash"))
            input->hash = newHashAllowEmpty(*hash, htUnknown);

        return input;
    }
};

static auto r1 = OnStartup([] { registerInputScheme(std::make_unique<TarballInputScheme>()); });

}
