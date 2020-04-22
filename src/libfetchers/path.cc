#include "fetchers.hh"
#include "store-api.hh"

namespace nix::fetchers {

struct PathInput : Input
{
    Path path;

    /* Allow the user to pass in "fake" tree info attributes. This is
       useful for making a pinned tree work the same as the repository
       from which is exported
       (e.g. path:/nix/store/...-source?lastModified=1585388205&rev=b0c285...). */
    std::optional<Hash> rev;
    std::optional<uint64_t> revCount;
    std::optional<time_t> lastModified;

    std::string type() const override { return "path"; }

    std::optional<Hash> getRev() const override { return rev; }

    bool operator ==(const Input & other) const override
    {
        auto other2 = dynamic_cast<const PathInput *>(&other);
        return
            other2
            && path == other2->path
            && rev == other2->rev
            && revCount == other2->revCount
            && lastModified == other2->lastModified;
    }

    bool isImmutable() const override
    {
        return (bool) narHash;
    }

    ParsedURL toURL() const override
    {
        auto query = attrsToQuery(toAttrsInternal());
        query.erase("path");
        return ParsedURL {
            .scheme = "path",
            .path = path,
            .query = query,
        };
    }

    Attrs toAttrsInternal() const override
    {
        Attrs attrs;
        attrs.emplace("path", path);
        if (rev)
            attrs.emplace("rev", rev->gitRev());
        if (revCount)
            attrs.emplace("revCount", *revCount);
        if (lastModified)
            attrs.emplace("lastModified", *lastModified);
        return attrs;
    }

    std::pair<Tree, std::shared_ptr<const Input>> fetchTreeInternal(nix::ref<Store> store) const override
    {
        auto input = std::make_shared<PathInput>(*this);

        // FIXME: check whether access to 'path' is allowed.

        auto storePath = store->maybeParseStorePath(path);

        if (storePath)
            store->addTempRoot(*storePath);

        if (!storePath || storePath->name() != "source" || !store->isValidPath(*storePath))
            // FIXME: try to substitute storePath.
            storePath = store->addToStore("source", path);

        return
            {
                Tree {
                    .actualPath = store->toRealPath(*storePath),
                    .storePath = std::move(*storePath),
                    .info = TreeInfo {
                        .revCount = revCount,
                        .lastModified = lastModified
                    }
                },
                input
            };
    }

};

struct PathInputScheme : InputScheme
{
    std::unique_ptr<Input> inputFromURL(const ParsedURL & url) override
    {
        if (url.scheme != "path") return nullptr;

        auto input = std::make_unique<PathInput>();
        input->path = url.path;

        for (auto & [name, value] : url.query)
            if (name == "rev")
                input->rev = Hash(value, htSHA1);
            else if (name == "revCount") {
                uint64_t revCount;
                if (!string2Int(value, revCount))
                    throw Error("path URL '%s' has invalid parameter '%s'", url.to_string(), name);
                input->revCount = revCount;
            }
            else if (name == "lastModified") {
                time_t lastModified;
                if (!string2Int(value, lastModified))
                    throw Error("path URL '%s' has invalid parameter '%s'", url.to_string(), name);
                input->lastModified = lastModified;
            }
            else
                throw Error("path URL '%s' has unsupported parameter '%s'", url.to_string(), name);

        return input;
    }

    std::unique_ptr<Input> inputFromAttrs(const Attrs & attrs) override
    {
        if (maybeGetStrAttr(attrs, "type") != "path") return {};

        auto input = std::make_unique<PathInput>();
        input->path = getStrAttr(attrs, "path");

        for (auto & [name, value] : attrs)
            if (name == "rev")
                input->rev = Hash(getStrAttr(attrs, "rev"), htSHA1);
            else if (name == "revCount")
                input->revCount = getIntAttr(attrs, "revCount");
            else if (name == "lastModified")
                input->lastModified = getIntAttr(attrs, "lastModified");
            else if (name == "type" || name == "path")
                ;
            else
                throw Error("unsupported path input attribute '%s'", name);

        return input;
    }
};

static auto r1 = OnStartup([] { registerInputScheme(std::make_unique<PathInputScheme>()); });

}
