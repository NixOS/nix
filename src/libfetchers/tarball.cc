#include "nix/fetchers/tarball.hh"
#include "nix/fetchers/fetchers.hh"
#include "nix/fetchers/cache.hh"
#include "nix/store/filetransfer.hh"
#include "nix/store/store-api.hh"
#include "nix/util/archive.hh"
#include "nix/util/tarfile.hh"
#include "nix/util/types.hh"
#include "nix/store/store-api.hh"
#include "nix/fetchers/git-utils.hh"
#include "nix/fetchers/fetch-settings.hh"

namespace nix::fetchers {

DownloadFileResult downloadFile(
    Store & store,
    const Settings & settings,
    const std::string & url,
    const std::string & name,
    const Headers & headers)
{
    // FIXME: check store

    Cache::Key key{
        "file",
        {{
            {"url", url},
            {"name", name},
        }}};

    auto cached = settings.getCache()->lookupStorePath(key, store);

    auto useCached = [&]() -> DownloadFileResult {
        return {
            .storePath = std::move(cached->storePath),
            .etag = getStrAttr(cached->value, "etag"),
            .effectiveUrl = getStrAttr(cached->value, "url"),
            .immutableUrl = maybeGetStrAttr(cached->value, "immutableUrl"),
        };
    };

    if (cached && !cached->expired)
        return useCached();

    FileTransferRequest request(VerbatimURL{url});
    request.headers = headers;
    if (cached)
        request.expectedETag = getStrAttr(cached->value, "etag");
    FileTransferResult res;
    try {
        res = getFileTransfer()->download(request);
    } catch (FileTransferError & e) {
        if (cached) {
            warn("%s; using cached version", e.msg());
            return useCached();
        } else
            throw;
    }

    Attrs infoAttrs({
        {"etag", res.etag},
    });

    if (res.immutableUrl)
        infoAttrs.emplace("immutableUrl", *res.immutableUrl);

    std::optional<StorePath> storePath;

    if (res.cached) {
        assert(cached);
        storePath = std::move(cached->storePath);
    } else {
        StringSink sink;
        dumpString(res.data, sink);
        auto hash = hashString(HashAlgorithm::SHA256, res.data);
        auto info = ValidPathInfo::makeFromCA(
            store,
            name,
            FixedOutputInfo{
                .method = FileIngestionMethod::Flat,
                .hash = hash,
                .references = {},
            },
            hashString(HashAlgorithm::SHA256, sink.s));
        info.narSize = sink.s.size();
        auto source = StringSource{sink.s};
        store.addToStore(info, source, NoRepair, NoCheckSigs);
        storePath = std::move(info.path);
    }

    /* Cache metadata for all URLs in the redirect chain. */
    for (auto & url : res.urls) {
        key.second.insert_or_assign("url", url);
        assert(!res.urls.empty());
        infoAttrs.insert_or_assign("url", *res.urls.rbegin());
        settings.getCache()->upsert(key, store, infoAttrs, *storePath);
    }

    return {
        .storePath = std::move(*storePath),
        .etag = res.etag,
        .effectiveUrl = *res.urls.rbegin(),
        .immutableUrl = res.immutableUrl,
    };
}

static DownloadTarballResult downloadTarball_(
    const Settings & settings, const std::string & urlS, const Headers & headers, const std::string & displayPrefix)
{
    ParsedURL url = parseURL(urlS);

    // Some friendly error messages for common mistakes.
    // Namely lets catch when the url is a local file path, but
    // it is not in fact a tarball.
    if (url.scheme == "file") {
        std::filesystem::path localPath = urlPathToPath(url.path);
        if (!localPath.is_absolute()) {
            throw Error(
                "tarball '%s' must use an absolute path. "
                "The 'file' scheme does not support relative paths.",
                url);
        }
        if (!exists(localPath)) {
            throw Error("tarball %s does not exist.", PathFmt(localPath));
        }
        if (is_directory(localPath)) {
            if (exists(localPath / ".git")) {
                throw Error(
                    "tarball %s is a git repository, not a tarball. Please use `git+file` as the scheme.",
                    PathFmt(localPath));
            }
            throw Error("tarball %s is a directory, not a file.", PathFmt(localPath));
        }
    }

    Cache::Key cacheKey{"tarball", {{"url", urlS}}};

    auto cached = settings.getCache()->lookupExpired(cacheKey);

    auto attrsToResult = [&](const Attrs & infoAttrs) {
        auto treeHash = getRevAttr(infoAttrs, "treeHash");
        return DownloadTarballResult{
            .treeHash = treeHash,
            .lastModified = (time_t) getIntAttr(infoAttrs, "lastModified"),
            .immutableUrl = maybeGetStrAttr(infoAttrs, "immutableUrl"),
            .accessor = settings.getTarballCache()->getAccessor(treeHash, {}, displayPrefix),
        };
    };

    if (cached && !settings.getTarballCache()->hasObject(getRevAttr(cached->value, "treeHash")))
        cached.reset();

    if (cached && !cached->expired)
        /* We previously downloaded this tarball and it's younger than
           `tarballTtl`, so no need to check the server. */
        return attrsToResult(cached->value);

    auto _res = std::make_shared<Sync<FileTransferResult>>();

    auto source = sinkToSource([&](Sink & sink) {
        FileTransferRequest req(url);
        req.expectedETag = cached ? getStrAttr(cached->value, "etag") : "";
        getFileTransfer()->download(std::move(req), sink, [_res](FileTransferResult r) { *_res->lock() = r; });
    });

    // TODO: fall back to cached value if download fails.

    auto act = std::make_unique<Activity>(*logger, lvlInfo, actUnknown, fmt("unpacking '%s' into the Git cache", url));

    AutoDelete cleanupTemp;

    /* Note: if the download is cached, `importTarball()` will receive
       no data, which causes it to import an empty tarball. */
    auto archive = !url.path.empty() && hasSuffix(toLower(url.path.back()), ".zip") ? ({
        /* In streaming mode, libarchive doesn't handle
           symlinks in zip files correctly (#10649). So write
           the entire file to disk so libarchive can access it
           in random-access mode. */
        auto [fdTemp, path] = createTempFile("nix-zipfile");
        cleanupTemp.cancel();
        cleanupTemp = {path};
        debug("downloading '%s' into '%s'...", url, path);
        {
            FdSink sink(fdTemp.get());
            source->drainInto(sink);
        }
        TarArchive{path};
    })
                                                                                    : TarArchive{*source};
    auto tarballCache = settings.getTarballCache();
    auto parseSink = tarballCache->getFileSystemObjectSink();
    auto lastModified = unpackTarfileToSink(archive, *parseSink);
    auto tree = parseSink->flush();

    act.reset();

    auto res(_res->lock());

    Attrs infoAttrs;

    if (res->cached) {
        /* The server says that the previously downloaded version is
           still current. */
        infoAttrs = cached->value;
    } else {
        infoAttrs.insert_or_assign("etag", res->etag);
        infoAttrs.insert_or_assign("treeHash", tarballCache->dereferenceSingletonDirectory(tree).gitRev());
        infoAttrs.insert_or_assign("lastModified", uint64_t(lastModified));
        if (res->immutableUrl)
            infoAttrs.insert_or_assign("immutableUrl", *res->immutableUrl);
    }

    /* Insert a cache entry for every URL in the redirect chain. */
    for (auto & url : res->urls) {
        cacheKey.second.insert_or_assign("url", url);
        settings.getCache()->upsert(cacheKey, infoAttrs);
    }

    // FIXME: add a cache entry for immutableUrl? That could allow
    // cache poisoning.

    return attrsToResult(infoAttrs);
}

ref<SourceAccessor> downloadTarball(Store & store, const Settings & settings, const std::string & url)
{
    /* Go through Input::getAccessor() to ensure that the resulting
       accessor has a fingerprint. */
    fetchers::Attrs attrs;
    attrs.insert_or_assign("type", "tarball");
    attrs.insert_or_assign("url", url);

    auto input = Input::fromAttrs(settings, std::move(attrs));

    return input.getAccessor(settings, store).first;
}

// An input scheme corresponding to a curl-downloadable resource.
struct CurlInputScheme : InputScheme
{
    const StringSet transportUrlSchemes = {"file", "http", "https"};

    bool hasTarballExtension(const ParsedURL & url) const
    {
        if (url.path.empty())
            return false;
        const auto & path = url.path.back();
        return hasSuffix(path, ".zip") || hasSuffix(path, ".tar") || hasSuffix(path, ".tgz")
               || hasSuffix(path, ".tar.gz") || hasSuffix(path, ".tar.xz") || hasSuffix(path, ".tar.bz2")
               || hasSuffix(path, ".tar.zst");
    }

    virtual bool isValidURL(const ParsedURL & url, bool requireTree) const = 0;

    static const StringSet specialParams;

    std::optional<Input>
    inputFromURL(const Settings & settings, const ParsedURL & _url, bool requireTree) const override
    {
        if (!isValidURL(_url, requireTree))
            return std::nullopt;

        Input input{};

        auto url = _url;

        url.scheme = parseUrlScheme(url.scheme).transport;

        auto narHash = url.query.find("narHash");
        if (narHash != url.query.end())
            input.attrs.insert_or_assign("narHash", narHash->second);

        if (auto i = get(url.query, "rev"))
            input.attrs.insert_or_assign("rev", *i);

        if (auto i = get(url.query, "revCount"))
            if (auto n = string2Int<uint64_t>(*i))
                input.attrs.insert_or_assign("revCount", *n);

        if (auto i = get(url.query, "lastModified"))
            if (auto n = string2Int<uint64_t>(*i))
                input.attrs.insert_or_assign("lastModified", *n);

        /* The URL query parameters serve two roles: specifying fetch
           settings for Nix itself, and arbitrary data as part of the
           HTTP request. Now that we've processed the Nix-specific
           attributes above, remove them so we don't also send them as
           part of the HTTP request. */
        for (auto & [param, _] : allowedAttrs())
            url.query.erase(param);

        input.attrs.insert_or_assign("type", std::string{schemeName()});
        input.attrs.insert_or_assign("url", url.to_string());
        return input;
    }

    static const std::map<std::string, AttributeInfo> & allowedAttrsImpl()
    {
        static const std::map<std::string, AttributeInfo> attrs = {
            {
                "url",
                {
                    .type = "String",
                    .required = true,
                    .doc = R"(
                      Supported protocols:

                      - `https`

                        > **Example**
                        >
                        > ```nix
                        > fetchTree {
                        >   type = "file";
                        >   url = "https://example.com/index.html";
                        > }
                        > ```

                      - `http`

                        Insecure HTTP transfer for legacy sources.

                        > **Warning**
                        >
                        > HTTP performs no encryption or authentication.
                        > Use a `narHash` known in advance to ensure the output has expected contents.

                      - `file`

                        A file on the local file system.

                        > **Example**
                        >
                        > ```nix
                        > fetchTree {
                        >   type = "file";
                        >   url = "file:///home/eelco/nix/README.md";
                        > }
                        > ```
                    )",
                },
            },
            {
                "narHash",
                {},
            },
            {
                "name",
                {},
            },
            {
                "unpack",
                {},
            },
            {
                "rev",
                {},
            },
            {
                "revCount",
                {},
            },
            {
                "lastModified",
                {},
            },
        };
        return attrs;
    }

    const std::map<std::string, AttributeInfo> & allowedAttrs() const override
    {
        return allowedAttrsImpl();
    }

    std::optional<Input> inputFromAttrs(const Settings & settings, const Attrs & attrs) const override
    {
        Input input{};
        input.attrs = attrs;

        // input.locked = (bool) maybeGetStrAttr(input.attrs, "hash");
        return input;
    }

    ParsedURL toURL(const Input & input) const override
    {
        auto url = parseURL(getStrAttr(input.attrs, "url"));
        // NAR hashes are preferred over file hashes since tar/zip
        // files don't have a canonical representation.
        if (auto narHash = input.getNarHash())
            url.query.insert_or_assign("narHash", narHash->to_string(HashFormat::SRI, true));
        return url;
    }

    bool isLocked(const Settings & settings, const Input & input) const override
    {
        return (bool) input.getNarHash();
    }
};

struct FileInputScheme : CurlInputScheme
{
    std::string_view schemeName() const override
    {
        return "file";
    }

    std::string schemeDescription() const override
    {
        return stripIndentation(R"(
          Place a plain file into the Nix store.
          This is similar to [`builtins.fetchurl`](@docroot@/language/builtins.md#builtins-fetchurl)
        )");
    }

    bool isValidURL(const ParsedURL & url, bool requireTree) const override
    {
        auto parsedUrlScheme = parseUrlScheme(url.scheme);
        return transportUrlSchemes.count(std::string(parsedUrlScheme.transport))
               && (parsedUrlScheme.application ? parsedUrlScheme.application.value() == schemeName()
                                               : (!requireTree && !hasTarballExtension(url)));
    }

    std::pair<ref<SourceAccessor>, Input>
    getAccessor(const Settings & settings, Store & store, const Input & _input) const override
    {
        auto input(_input);

        /* Unlike TarballInputScheme, this stores downloaded files in
           the Nix store directly, since there is little deduplication
           benefit in using the Git cache for single big files like
           tarballs. */
        auto file = downloadFile(store, settings, getStrAttr(input.attrs, "url"), input.getName());

        auto narHash = store.queryPathInfo(file.storePath)->narHash;
        input.attrs.insert_or_assign("narHash", narHash.to_string(HashFormat::SRI, true));

        auto accessor = ref{store.getFSAccessor(file.storePath)};

        accessor->setPathDisplay("«" + input.to_string() + "»");

        return {accessor, input};
    }
};

struct TarballInputScheme : CurlInputScheme
{
    std::string_view schemeName() const override
    {
        return "tarball";
    }

    std::string schemeDescription() const override
    {
        return stripIndentation(R"(
          Download a tar archive and extract it into the Nix store.
          This has the same underlying implementation as [`builtins.fetchTarball`](@docroot@/language/builtins.md#builtins-fetchTarball)
        )");
    }

    const std::map<std::string, AttributeInfo> & allowedAttrs() const override
    {
        static const std::map<std::string, AttributeInfo> attrs = [] {
            auto attrs = CurlInputScheme::allowedAttrsImpl();
            // Override the "url" attribute to add tarball-specific example
            attrs["url"].doc = R"(
              > **Example**
              >
              > ```nix
              > fetchTree {
              >   type = "tarball";
              >   url = "https://github.com/NixOS/nixpkgs/tarball/nixpkgs-23.11";
              > }
              > ```
            )";
            return attrs;
        }();
        return attrs;
    }

    bool isValidURL(const ParsedURL & url, bool requireTree) const override
    {
        auto parsedUrlScheme = parseUrlScheme(url.scheme);

        return transportUrlSchemes.count(std::string(parsedUrlScheme.transport))
               && (parsedUrlScheme.application ? parsedUrlScheme.application.value() == schemeName()
                                               : (requireTree || hasTarballExtension(url)));
    }

    std::pair<ref<SourceAccessor>, Input>
    getAccessor(const Settings & settings, Store & store, const Input & _input) const override
    {
        auto input(_input);

        auto result = downloadTarball_(settings, getStrAttr(input.attrs, "url"), {}, "«" + input.to_string() + "»");

        if (result.immutableUrl) {
            auto immutableInput = Input::fromURL(settings, *result.immutableUrl);
            // FIXME: would be nice to support arbitrary flakerefs
            // here, e.g. git flakes.
            if (immutableInput.getType() != "tarball")
                throw Error("tarball 'Link' headers that redirect to non-tarball URLs are not supported");
            input = immutableInput;
        }

        if (result.lastModified && !input.attrs.contains("lastModified"))
            input.attrs.insert_or_assign("lastModified", uint64_t(result.lastModified));

        input.attrs.insert_or_assign(
            "narHash",
            settings.getTarballCache()->treeHashToNarHash(settings, result.treeHash).to_string(HashFormat::SRI, true));

        return {result.accessor, input};
    }

    std::optional<std::string> getFingerprint(Store & store, const Input & input) const override
    {
        if (auto narHash = input.getNarHash())
            return narHash->to_string(HashFormat::SRI, true);
        else if (auto rev = input.getRev())
            return rev->gitRev();
        else
            return std::nullopt;
    }
};

static auto rTarballInputScheme = OnStartup([] { registerInputScheme(std::make_unique<TarballInputScheme>()); });
static auto rFileInputScheme = OnStartup([] { registerInputScheme(std::make_unique<FileInputScheme>()); });

} // namespace nix::fetchers
