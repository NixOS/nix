#include "fetchers.hh"
#include "store-api.hh"
#include "archive.hh"

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
                if (auto n = string2Int<uint64_t>(value))
                    input.attrs.insert_or_assign(name, *n);
                else
                    throw Error("path URL '%s' has invalid parameter '%s'", url.to_string(), name);
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

    bool isRelative(const Input & input) const override
    {
        return !hasPrefix(*input.getSourcePath(), "/");
    }

    bool hasAllInfo(const Input & input) override
    {
        return true;
    }

    std::optional<Path> getSourcePath(const Input & input) override
    {
        return getStrAttr(input.attrs, "path");
    }

    void markChangedFile(const Input & input, std::string_view file, std::optional<std::string> commitMsg) override
    {
        // nothing to do
    }

    CanonPath getAbsPath(ref<Store> store, const Input & input) const
    {
        auto path = getStrAttr(input.attrs, "path");

        if (path[0] == '/')
            return CanonPath(path);

        // FIXME: remove this?
        if (!input.parent)
            throw Error("cannot fetch input '%s' because it uses a relative path", input.to_string());

        CanonPath parent(*input.parent);

        // the path isn't relative, prefix it
        auto absPath = CanonPath(path, parent);

        // for security, ensure that if the parent is a store path, it's inside it
        if (store->isInStore(parent.abs())) {
            auto storePath = store->printStorePath(store->toStorePath(parent.abs()).first);
            if (!absPath.isWithin(CanonPath(storePath)))
                throw BadStorePath("relative path '%s' points outside of its parent's store path '%s'", path, storePath);
        }

        return absPath;
    }

    std::pair<StorePath, Input> fetch(ref<Store> store, const Input & _input) override
    {
        Input input(_input);

        auto absPath = getAbsPath(store, input);

        Activity act(*logger, lvlTalkative, actUnknown, fmt("copying '%s'", absPath));

        // FIXME: check whether access to 'path' is allowed.
        auto storePath = store->maybeParseStorePath(absPath.abs());

        if (storePath)
            store->addTempRoot(*storePath);

        time_t mtime = 0;
        if (!storePath || storePath->name() != "source" || !store->isValidPath(*storePath)) {
            // FIXME: try to substitute storePath.
            auto src = sinkToSource([&](Sink & sink) {
                mtime = dumpPathAndGetMtime(absPath.abs(), sink, defaultPathFilter);
            });
            storePath = store->addToStoreFromDump(*src, "source");
        }
        input.attrs.insert_or_assign("lastModified", uint64_t(mtime));

        return {std::move(*storePath), input};
    }

    std::pair<ref<InputAccessor>, Input> lazyFetch(ref<Store> store, const Input & input) override
    {
        auto absPath = getAbsPath(store, input);
        auto input2(input);
        input2.attrs.emplace("path", (std::string) absPath.abs());
        return {makeFSInputAccessor(absPath), std::move(input2)};
    }

    std::optional<std::string> getFingerprint(ref<Store> store, const Input & input) const override
    {
        /* If this path is in the Nix store, we can consider it
           locked, so just use the path as its fingerprint. Maybe we
           should restrict this to CA paths but that's not
           super-important. */
        auto path = getAbsPath(store, input);
        if (store->isInStore(path.abs()))
            return path.abs();
        return std::nullopt;
    }

};

static auto rPathInputScheme = OnStartup([] { registerInputScheme(std::make_unique<PathInputScheme>()); });

}
