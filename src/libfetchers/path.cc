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

    std::optional<CanonPath> isRelative(const Input & input) const override
    {
        auto path = getStrAttr(input.attrs, "path");
        if (hasPrefix(path, "/"))
            return std::nullopt;
        else
            return CanonPath(path);
    }

    bool hasAllInfo(const Input & input) override
    {
        return true;
    }

    void putFile(
        const Input & input,
        const CanonPath & path,
        std::string_view contents,
        std::optional<std::string> commitMsg) const
    {
        auto absPath = CanonPath(getAbsPath(input)) + path;

        // FIXME: make sure that absPath is not a symlink that escapes
        // the repo.
        writeFile(absPath.abs(), contents);
    }

    CanonPath getAbsPath(const Input & input) const
    {
        auto path = getStrAttr(input.attrs, "path");

        if (path[0] == '/')
            return CanonPath(path);

        throw Error("cannot fetch input '%s' because it uses a relative path", input.to_string());
    }

    std::pair<ref<InputAccessor>, Input> getAccessor(ref<Store> store, const Input & input) override
    {
        auto absPath = getAbsPath(input);
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
        auto path = getAbsPath(input);
        if (store->isInStore(path.abs()))
            return path.abs();
        return std::nullopt;
    }

};

static auto rPathInputScheme = OnStartup([] { registerInputScheme(std::make_unique<PathInputScheme>()); });

}
