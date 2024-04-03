#include "fetchers.hh"
#include "store-api.hh"
#include "archive.hh"
#include "fs-input-accessor.hh"
#include "posix-source-accessor.hh"

namespace nix::fetchers {

struct PathInputScheme : InputScheme
{
    std::optional<Input> inputFromURL(const ParsedURL & url, bool requireTree) const override
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

    std::string_view schemeName() const override
    {
        return "path";
    }

    StringSet allowedAttrs() const override
    {
        return {
            "path",
            /* Allow the user to pass in "fake" tree info
               attributes. This is useful for making a pinned tree work
               the same as the repository from which is exported (e.g.
               path:/nix/store/...-source?lastModified=1585388205&rev=b0c285...).
             */
            "rev",
            "revCount",
            "lastModified",
            "narHash",
        };
    }
    std::optional<Input> inputFromAttrs(const Attrs & attrs) const override
    {
        getStrAttr(attrs, "path");

        Input input;
        input.attrs = attrs;
        return input;
    }

    ParsedURL toURL(const Input & input) const override
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

    std::optional<Path> getSourcePath(const Input & input) const override
    {
        return getStrAttr(input.attrs, "path");
    }

    void putFile(
        const Input & input,
        const CanonPath & path,
        std::string_view contents,
        std::optional<std::string> commitMsg) const override
    {
        writeFile((CanonPath(getAbsPath(input)) / path).abs(), contents);
    }

    std::optional<std::string> isRelative(const Input & input) const
    {
        auto path = getStrAttr(input.attrs, "path");
        if (hasPrefix(path, "/"))
            return std::nullopt;
        else
            return path;
    }

    bool isLocked(const Input & input) const override
    {
        return (bool) input.getNarHash();
    }

    CanonPath getAbsPath(const Input & input) const
    {
        auto path = getStrAttr(input.attrs, "path");

        if (path[0] == '/')
            return CanonPath(path);

        throw Error("cannot fetch input '%s' because it uses a relative path", input.to_string());
    }

    std::pair<ref<InputAccessor>, Input> getAccessor(ref<Store> store, const Input & _input) const override
    {
        Input input(_input);
        std::string absPath;
        auto path = getStrAttr(input.attrs, "path");

        if (path[0] != '/') {
            if (!input.parent)
                throw Error("cannot fetch input '%s' because it uses a relative path", input.to_string());

            auto parent = canonPath(*input.parent);

            // the path isn't relative, prefix it
            absPath = nix::absPath(path, parent);

            // for security, ensure that if the parent is a store path, it's inside it
            if (store->isInStore(parent)) {
                auto storePath = store->printStorePath(store->toStorePath(parent).first);
                if (!isDirOrInDir(absPath, storePath))
                    throw BadStorePath("relative path '%s' points outside of its parent's store path '%s'", path, storePath);
            }
        } else
            absPath = path;

        Activity act(*logger, lvlTalkative, actUnknown, fmt("copying '%s'", absPath));

        // FIXME: check whether access to 'path' is allowed.
        auto storePath = store->maybeParseStorePath(absPath);

        if (storePath)
            store->addTempRoot(*storePath);

        time_t mtime = 0;
        if (!storePath || storePath->name() != "source" || !store->isValidPath(*storePath)) {
            // FIXME: try to substitute storePath.
            auto src = sinkToSource([&](Sink & sink) {
                mtime = dumpPathAndGetMtime(absPath, sink, defaultPathFilter);
            });
            storePath = store->addToStoreFromDump(*src, "source");
        }
        input.attrs.insert_or_assign("lastModified", uint64_t(mtime));

        return {makeStorePathAccessor(store, *storePath), std::move(input)};
    }

    std::optional<std::string> getFingerprint(ref<Store> store, const Input & input) const override
    {
        if (isRelative(input))
            return std::nullopt;

        /* If this path is in the Nix store, use the hash of the
           store object and the subpath. */
        auto path = getAbsPath(input);
        try {
            auto [storePath, subPath] = store->toStorePath(path.abs());
            auto info = store->queryPathInfo(storePath);
            return fmt("path:%s:%s", info->narHash.to_string(HashFormat::Base16, false), subPath);
        } catch (Error &) {
            return std::nullopt;
        }
    }

    std::optional<ExperimentalFeature> experimentalFeature() const override
    {
        return Xp::Flakes;
    }
};

static auto rPathInputScheme = OnStartup([] { registerInputScheme(std::make_unique<PathInputScheme>()); });

}
