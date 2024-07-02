#include "fetchers.hh"
#include "store-api.hh"
#include "archive.hh"
#include "store-path-accessor.hh"

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
            else if (name == "lock")
                input.attrs.emplace(name, Explicit<bool> { value == "1" });
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
            "lock",
        };
    }

    std::optional<Input> inputFromAttrs(const Attrs & attrs) const override
    {
        getStrAttr(attrs, "path");
        maybeGetBoolAttr(attrs, "lock");

        Input input;
        input.attrs = attrs;
        return input;
    }

    bool getLockAttr(const Input & input) const
    {
        // FIXME: make the default "true"?
        return maybeGetBoolAttr(input.attrs, "lock").value_or(false);
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

    void putFile(
        const Input & input,
        const CanonPath & path,
        std::string_view contents,
        std::optional<std::string> commitMsg) const override
    {
        writeFile((CanonPath(getAbsPath(input)) / path).abs(), contents);
    }

    std::optional<std::string> isRelative(const Input & input) const override
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

    std::pair<ref<SourceAccessor>, Input> getAccessor(ref<Store> store, const Input & input) const override
    {
        auto absPath = getAbsPath(input);
        auto input2(input);
        input2.attrs.emplace("path", (std::string) absPath.abs());

        if (getLockAttr(input2)) {

            auto storePath = store->maybeParseStorePath(absPath.abs());

            if (!storePath || storePath->name() != input.getName() || !store->isValidPath(*storePath)) {
                Activity act(*logger, lvlChatty, actUnknown, fmt("copying '%s' to the store", absPath));
                storePath = store->addToStore(input.getName(), {getFSSourceAccessor(), absPath});
                auto narHash = store->queryPathInfo(*storePath)->narHash;
                input2.attrs.insert_or_assign("narHash", narHash.to_string(HashFormat::SRI, true));
            } else
                input2.attrs.erase("narHash");

            input2.attrs.erase("lastModified");

            #if 0
            // FIXME: produce a better error message if the path does
            // not exist in the source directory.
            auto makeNotAllowedError = [absPath](const CanonPath & path) -> RestrictedPathError
            {
                return RestrictedPathError("path '%s' does not exist'", absPath + path);
            };
            #endif

            return {makeStorePathAccessor(store, *storePath), std::move(input2)};

        } else {
            auto accessor = makeFSSourceAccessor(std::filesystem::path(absPath.abs()));
            accessor->setPathDisplay(absPath.abs());
            return {accessor, std::move(input2)};
        }
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
