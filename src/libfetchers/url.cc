#include "fetchers.hh"
#include "cache.hh"
#include "filetransfer.hh"
#include "globals.hh"
#include "store-api.hh"
#include "archive.hh"
#include "tarfile.hh"
#include "types.hh"

namespace nix::fetchers {

std::pair<Tree, time_t> downloadRawFile(
    ref<Store> store,
    const std::string & url,
    const std::string & name,
    bool immutable,
    const Headers & headers)
{
    Attrs inAttrs({
        {"type", "url"},
        {"url", url},
        {"name", name},
    });

    auto cached = getCache()->lookupExpired(store, inAttrs);

    if (cached && !cached->expired)
        return {
            Tree(store->toRealPath(cached->storePath), std::move(cached->storePath)),
            getIntAttr(cached->infoAttrs, "lastModified")
        };

    auto res = downloadFile(store, url, name, immutable, headers);

    std::optional<StorePath> unpackedStorePath;
    time_t lastModified;

    if (cached && res.etag != "" && getStrAttr(cached->infoAttrs, "etag") == res.etag) {
        unpackedStorePath = std::move(cached->storePath);
        lastModified = getIntAttr(cached->infoAttrs, "lastModified");
    } else {
        lastModified = lstat(store->toRealPath(cached->storePath)).st_mtime;
    }

    Attrs infoAttrs({
        {"lastModified", uint64_t(lastModified)},
        {"etag", res.etag},
    });

    getCache()->add(
        store,
        inAttrs,
        infoAttrs,
        res.storePath,
        immutable);

    return {
        Tree(store->toRealPath(*unpackedStorePath), std::move(*unpackedStorePath)),
        lastModified,
    };
}

struct UrlInputScheme : InputScheme
{
    std::optional<Input> inputFromURL(const ParsedURL & url) override
    {
        if (url.scheme != "file" && url.scheme != "http" && url.scheme != "https") return {};

        if (hasSuffix(url.path, ".zip")
            || hasSuffix(url.path, ".tar")
            || hasSuffix(url.path, ".tgz")
            || hasSuffix(url.path, ".tar.gz")
            || hasSuffix(url.path, ".tar.xz")
            || hasSuffix(url.path, ".tar.bz2")
            || hasSuffix(url.path, ".tar.zst"))
            return {};

        Input input;
        input.attrs.insert_or_assign("type", "url");
        input.attrs.insert_or_assign("flake", false);
        input.attrs.insert_or_assign("url", url.to_string());
        auto narHash = url.query.find("narHash");
        if (narHash != url.query.end())
            input.attrs.insert_or_assign("narHash", narHash->second);
        return input;
    }

    std::optional<Input> inputFromAttrs(const Attrs & attrs) override
    {
        if (maybeGetStrAttr(attrs, "type") != "url") return {};

        for (auto & [name, value] : attrs)
            if (name != "type" && name != "url" && name != "flake" && name != "narHash" && name != "name")
                throw Error("unsupported url input attribute '%s'", name);

        Input input;
        input.attrs = attrs;
        //input.immutable = (bool) maybeGetStrAttr(input.attrs, "hash");
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

    std::pair<Tree, Input> fetch(ref<Store> store, const Input & input) override
    {
        auto tree = downloadFile(store, getStrAttr(input.attrs, "url"), input.getName(), false);
        return {
            Tree(store->toRealPath(tree.storePath), std::move(tree.storePath)),
            input,
        };
    }
};

static auto rUrlInputScheme = OnStartup([] { registerInputScheme(std::make_unique<UrlInputScheme>()); });

}
