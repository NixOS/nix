#include "fetchers.hh"
#include "store-api.hh"

namespace nix::fetchers {

struct PathInputScheme : InputScheme
{
    std::optional<Input> inputFromURL(const ParsedURL & url) override
    {
        if (url.scheme != "path") return {};

        if (url.authority && *url.authority != "")
            throw Error("path URL '%s' should not have an authority ('%s')", url.url, *url.authority);

        Input input;
        input.attrs.insert_or_assign("type", "path");
        input.attrs.insert_or_assign("path", url.path);

        for (auto & [name, value] : url.query)
            if (name == "rev" || name == "narHash")
                input.attrs.insert_or_assign(name, value);
            else if (name == "revCount" || name == "lastModified") {
                uint64_t n;
                if (!string2Int(value, n))
                    throw Error("path URL '%s' has invalid parameter '%s'", url.to_string(), name);
                input.attrs.insert_or_assign(name, n);
            }
            else
                throw Error("path URL '%s' has unsupported parameter '%s'", url.to_string(), name);

        return input;
    }

    std::optional<Input> inputFromAttrs(const Attrs & attrs) override
    {
        if (maybeGetStrAttr(attrs, "type") != "path") return {};

        getStrAttr(attrs, "path");

        for (auto & [name, value] : attrs)
            /* Allow the user to pass in "fake" tree info
               attributes. This is useful for making a pinned tree
               work the same as the repository from which is exported
               (e.g. path:/nix/store/...-source?lastModified=1585388205&rev=b0c285...). */
            if (name == "type" || name == "rev" || name == "revCount" || name == "lastModified" || name == "narHash" || name == "path")
                // checked in Input::fromAttrs
                ;
            else
                throw Error("unsupported path input attribute '%s'", name);

        Input input;
        input.attrs = attrs;
        return input;
    }

    ParsedURL toURL(const Input & input) override
    {
        auto query = attrsToQuery(input.attrs);
        query.erase("path");
        query.erase("type");
        return ParsedURL {
            .scheme = "path",
            .path = getStrAttr(input.attrs, "path"),
            .query = query,
        };
    }

    bool hasAllInfo(const Input & input) override
    {
        return (bool) input.getNarHash();
    }

    std::optional<Path> getSourcePath(const Input & input) override
    {
        return getStrAttr(input.attrs, "path");
    }

    void markChangedFile(const Input & input, std::string_view file, std::optional<std::string> commitMsg) override
    {
        // nothing to do
    }

    std::pair<Tree, Input> fetch(ref<Store> store, const Input & input) override
    {
        auto path = getStrAttr(input.attrs, "path");

        // FIXME: check whether access to 'path' is allowed.

        auto storePath = store->maybeParseStorePath(path);

        if (storePath)
            store->addTempRoot(*storePath);

        if (!storePath || storePath->name() != "source" || !store->isValidPath(*storePath)) {
            // FIXME: try to substitute storePath.
            storePath = store->addToStore("source", path);
        }

        // FIXME: just have Store::addToStore return a StorePathDescriptor, as
        // it has the underlying information.
        auto storePathDesc = store->queryPathInfo(*storePath)->fullStorePathDescriptorOpt().value();

        return {
            Tree {
                store->toRealPath(store->makeFixedOutputPathFromCA(storePathDesc)),
                std::move(storePathDesc),
            },
            input
        };
    }
};

static auto r1 = OnStartup([] { registerInputScheme(std::make_unique<PathInputScheme>()); });

}
