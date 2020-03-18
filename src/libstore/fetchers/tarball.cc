#include "fetchers/fetchers.hh"
#include "fetchers/parse.hh"
#include "fetchers/cache.hh"
#include "download.hh"
#include "globals.hh"
#include "store-api.hh"
#include "archive.hh"
#include "tarfile.hh"

namespace nix::fetchers {

StorePath downloadFile(
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

    if (auto res = getCache()->lookup(store, inAttrs))
        return std::move(res->second);

    // FIXME: use ETag.

    DownloadRequest request(url);
    auto res = getDownloader()->download(request);

    // FIXME: write to temporary file.

    StringSink sink;
    dumpString(*res.data, sink);
    auto hash = hashString(htSHA256, *res.data);
    ValidPathInfo info(store->makeFixedOutputPath(false, hash, name));
    info.narHash = hashString(htSHA256, *sink.s);
    info.narSize = sink.s->size();
    info.ca = makeFixedOutputCA(false, hash);
    store->addToStore(info, sink.s, NoRepair, NoCheckSigs);

    Attrs infoAttrs({
        {"etag", res.etag},
    });

    getCache()->add(
        store,
        inAttrs,
        infoAttrs,
        info.path.clone(),
        immutable);

    return std::move(info.path);
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

    if (auto res = getCache()->lookup(store, inAttrs))
        return Tree {
            .actualPath = store->toRealPath(res->second),
            .storePath = std::move(res->second),
            .info = TreeInfo {
                .lastModified = getIntAttr(res->first, "lastModified"),
            },
        };

    auto tarball = downloadFile(store, url, name, immutable);

    Path tmpDir = createTempDir();
    AutoDelete autoDelete(tmpDir, true);
    unpackTarfile(store->toRealPath(tarball), tmpDir);
    auto members = readDirectory(tmpDir);
    if (members.size() != 1)
        throw nix::Error("tarball '%s' contains an unexpected number of top-level files", url);
    auto topDir = tmpDir + "/" + members.begin()->name;
    auto lastModified = lstat(topDir).st_mtime;
    auto unpackedStorePath = store->addToStore(name, topDir, true, htSHA256, defaultPathFilter, NoRepair);

    Attrs infoAttrs({
        {"lastModified", lastModified},
    });

    getCache()->add(
        store,
        inAttrs,
        infoAttrs,
        unpackedStorePath,
        immutable);

    return Tree {
        .actualPath = store->toRealPath(unpackedStorePath),
        .storePath = std::move(unpackedStorePath),
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

    std::string to_string() const override
    {
        auto url2(url);
        // NAR hashes are preferred over file hashes since tar/zip files
        // don't have a canonical representation.
        if (narHash)
            url2.query.insert_or_assign("narHash", narHash->to_string(SRI));
        else if (hash)
            url2.query.insert_or_assign("hash", hash->to_string(SRI));
        return url2.to_string();
    }

    Attrs toAttrsInternal() const override
    {
        Attrs attrs;
        attrs.emplace("url", url.to_string());
        if (narHash)
            attrs.emplace("narHash", hash->to_string(SRI));
        else if (hash)
            attrs.emplace("hash", hash->to_string(SRI));
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
            if (name != "type" && name != "url" && name != "hash" && name != "narHash")
                throw Error("unsupported tarball input attribute '%s'", name);

        auto input = std::make_unique<TarballInput>(parseURL(getStrAttr(attrs, "url")));
        if (auto hash = maybeGetStrAttr(attrs, "hash"))
            // FIXME: require SRI hash.
            input->hash = Hash(*hash);

        return input;
    }
};

static auto r1 = OnStartup([] { registerInputScheme(std::make_unique<TarballInputScheme>()); });

}
