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
        return (bool) hash;
    }

    std::string to_string() const override
    {
        auto url2(url);
        if (narHash)
            url2.query.insert_or_assign("narHash", narHash->to_string(SRI));
        return url2.to_string();
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
                .lastModified = *res.lastModified
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

        auto input = std::make_unique<TarballInput>();
        input->type = "tarball";
        input->url = url;

        auto narHash = url.query.find("narHash");
        if (narHash != url.query.end()) {
            // FIXME: require SRI hash.
            input->narHash = Hash(narHash->second);
        }

        return input;
    }
};

static auto r1 = OnStartup([] { registerInputScheme(std::make_unique<TarballInputScheme>()); });

}
