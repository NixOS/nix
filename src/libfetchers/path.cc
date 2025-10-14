#include "nix/fetchers/fetchers.hh"
#include "nix/store/store-api.hh"
#include "nix/util/archive.hh"
#include "nix/fetchers/cache.hh"
#include "nix/fetchers/fetch-to-store.hh"
#include "nix/fetchers/fetch-settings.hh"

namespace nix::fetchers {

struct PathInputScheme : InputScheme
{
    std::optional<Input> inputFromURL(const Settings & settings, const ParsedURL & url, bool requireTree) const override
    {
        if (url.scheme != "path")
            return {};

        if (url.authority && url.authority->host.size())
            throw Error("path URL '%s' should not have an authority ('%s')", url, *url.authority);

        Input input{settings};
        input.attrs.insert_or_assign("type", "path");
        input.attrs.insert_or_assign("path", renderUrlPathEnsureLegal(url.path));

        for (auto & [name, value] : url.query)
            if (name == "rev" || name == "narHash")
                input.attrs.insert_or_assign(name, value);
            else if (name == "revCount" || name == "lastModified") {
                if (auto n = string2Int<uint64_t>(value))
                    input.attrs.insert_or_assign(name, *n);
                else
                    throw Error("path URL '%s' has invalid parameter '%s'", url, name);
            } else
                throw Error("path URL '%s' has unsupported parameter '%s'", url, name);

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

    std::optional<Input> inputFromAttrs(const Settings & settings, const Attrs & attrs) const override
    {
        getStrAttr(attrs, "path");

        Input input{settings};
        input.attrs = attrs;
        return input;
    }

    ParsedURL toURL(const Input & input) const override
    {
        auto query = attrsToQuery(input.attrs);
        query.erase("path");
        query.erase("type");
        query.erase("__final");
        return ParsedURL{
            .scheme = "path",
            .path = splitString<std::vector<std::string>>(getStrAttr(input.attrs, "path"), "/"),
            .query = query,
        };
    }

    std::optional<std::filesystem::path> getSourcePath(const Input & input) const override
    {
        return getAbsPath(input);
    }

    void putFile(
        const Input & input,
        const CanonPath & path,
        std::string_view contents,
        std::optional<std::string> commitMsg) const override
    {
        writeFile(getAbsPath(input) / path.rel(), contents);
    }

    std::optional<std::string> isRelative(const Input & input) const override
    {
        auto path = getStrAttr(input.attrs, "path");
        if (isAbsolute(path))
            return std::nullopt;
        else
            return path;
    }

    bool isLocked(const Input & input) const override
    {
        return (bool) input.getNarHash();
    }

    std::filesystem::path getAbsPath(const Input & input) const
    {
        auto path = getStrAttr(input.attrs, "path");

        if (isAbsolute(path))
            return canonPath(path);

        throw Error("cannot fetch input '%s' because it uses a relative path", input.to_string());
    }

    std::pair<ref<SourceAccessor>, Input> getAccessor(ref<Store> store, const Input & _input) const override
    {
        Input input(_input);
        auto path = getStrAttr(input.attrs, "path");

        auto absPath = getAbsPath(input);

        // FIXME: check whether access to 'path' is allowed.
        auto storePath = store->maybeParseStorePath(absPath.string());

        if (storePath)
            store->addTempRoot(*storePath);

        time_t mtime = 0;
        if (!storePath || storePath->name() != "source" || !store->isValidPath(*storePath)) {
            Activity act(*logger, lvlTalkative, actUnknown, fmt("copying %s to the store", absPath));
            // FIXME: try to substitute storePath.
            auto src = sinkToSource(
                [&](Sink & sink) { mtime = dumpPathAndGetMtime(absPath.string(), sink, defaultPathFilter); });
            storePath = store->addToStoreFromDump(*src, "source");
        }

        auto accessor = store->requireStoreObjectAccessor(*storePath);

        // To prevent `fetchToStore()` copying the path again to Nix
        // store, pre-create an entry in the fetcher cache.
        auto info = store->queryPathInfo(*storePath);
        accessor->fingerprint =
            fmt("path:%s", store->queryPathInfo(*storePath)->narHash.to_string(HashFormat::SRI, true));
        input.settings->getCache()->upsert(
            makeFetchToStoreCacheKey(
                input.getName(), *accessor->fingerprint, ContentAddressMethod::Raw::NixArchive, "/"),
            *store,
            {},
            *storePath);

        /* Trust the lastModified value supplied by the user, if
           any. It's not a "secure" attribute so we don't care. */
        if (!input.getLastModified())
            input.attrs.insert_or_assign("lastModified", uint64_t(mtime));

        return {accessor, std::move(input)};
    }

    std::optional<ExperimentalFeature> experimentalFeature() const override
    {
        return Xp::Flakes;
    }
};

static auto rPathInputScheme = OnStartup([] { registerInputScheme(std::make_unique<PathInputScheme>()); });

} // namespace nix::fetchers
