#include "fetchers.hh"
#include "download.hh"
#include "globals.hh"
#include "parse.hh"
#include "store-api.hh"

namespace nix::fetchers {

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
        CachedDownloadRequest request(url.to_string());
        request.unpack = true;
        request.getLastModified = true;
        request.name = "source";

        auto res = getDownloader()->downloadCached(store, request);

        auto input = std::make_shared<TarballInput>(*this);

        auto storePath = store->parseStorePath(res.storePath);

        input->narHash = store->queryPathInfo(storePath)->narHash;

        return {
            Tree {
                .actualPath = res.path,
                .storePath = std::move(storePath),
                .info = TreeInfo {
                    .lastModified = *res.lastModified,
                },
            },
            input
        };
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

        auto hash = url.query.find("hash");
        if (hash != url.query.end())
            // FIXME: require SRI hash.
            input->hash = Hash(hash->second);

        auto narHash = url.query.find("narHash");
        if (narHash != url.query.end())
            // FIXME: require SRI hash.
            input->narHash = Hash(narHash->second);

        return input;
    }

    std::unique_ptr<Input> inputFromAttrs(const Input::Attrs & attrs) override
    {
        if (maybeGetStrAttr(attrs, "type") != "tarball") return {};

        for (auto & [name, value] : attrs)
            if (name != "type" && name != "url" && name != "hash" && name != "narHash")
                throw Error("unsupported tarball input attribute '%s'", name);

        auto input = std::make_unique<TarballInput>(parseURL(getStrAttr(attrs, "url")));
        if (auto hash = maybeGetStrAttr(attrs, "hash"))
            // FIXME: require SRI hash.
            input->hash = Hash(*hash);
        if (auto narHash = maybeGetStrAttr(attrs, "narHash"))
            // FIXME: require SRI hash.
            input->narHash = Hash(*narHash);
        return input;
    }
};

static auto r1 = OnStartup([] { registerInputScheme(std::make_unique<TarballInputScheme>()); });

}
