#include "nix/store/filetransfer.hh"
#include "nix/fetchers/cache.hh"
#include "nix/store/store-api.hh"
#include "nix/util/types.hh"
#include "nix/util/url-parts.hh"
#include "nix/util/url.hh"
#include "nix/util/git.hh"
#include "nix/util/strings.hh"
#include "nix/fetchers/fetchers.hh"
#include "nix/fetchers/fetch-settings.hh"
#include "nix/fetchers/tarball.hh"
#include "nix/util/tarfile.hh"
#include "nix/fetchers/git-utils.hh"

#include <optional>
#include <nlohmann/json.hpp>
#include <vector>

namespace nix::fetchers {

struct DownloadUrl
{
    ParsedURL url;
    Headers headers;
};

// A github, gitlab, sourcehut, gitea/forgejo, cgit, or bitbucket host.
// The optional port is needed for URL-authority flake refs such as
// gitlab://gitlab.example.org:8443/group/repo?ref=main.
const static std::string hostRegexS = "[a-zA-Z0-9.-]+(:[0-9]+)?"; // FIXME: check
std::regex hostRegex(hostRegexS, std::regex::ECMAScript);

static std::string authorityHostWithOptionalPort(const ParsedURL::Authority & authority)
{
    if (authority.user || authority.password)
        throw BadURL("URL authority for Git forge input schemes must not include user information");

    auto host = authority.host;
    if (host.empty())
        throw BadURL("URL authority for Git forge input schemes must include a host");

    if (authority.port)
        host += fmt(":%d", *authority.port);

    if (!std::regex_match(host, hostRegex))
        throw BadURL("URL authority for Git forge input schemes contains an invalid host '%s'", host);

    return host;
}

static std::string
joinPathSegments(std::vector<std::string>::const_iterator begin, std::vector<std::string>::const_iterator end)
{
    std::string result;
    for (auto i = begin; i != end; ++i) {
        if (!result.empty())
            result += "/";
        result += *i;
    }
    return result;
}

static std::vector<std::string> splitPathSegments(const std::string & s)
{
    return splitString<std::vector<std::string>>(s, "/");
}

static nlohmann::json
downloadJSON(Store & store, const Settings & settings, const std::string & url, const Headers & headers)
{
    auto downloadResult = downloadFile(store, settings, url, "source", headers);
    return nlohmann::json::parse(store.requireStoreObjectAccessor(downloadResult.storePath)->readFile(CanonPath::root));
}

enum class ForgePathMode {
    OwnerRepo,
    NestedOwnerRepo,
    NestedOwnerRepoAllowRootRepo,
};

enum class RefSyntax {
    QueryOnly,
    LegacyPathAllowed,
};

struct ForgeRepoPath
{
    std::string owner;
    std::string repo;
};

static bool supportsNestedRepositoryPaths(ForgePathMode mode)
{
    return mode == ForgePathMode::NestedOwnerRepo || mode == ForgePathMode::NestedOwnerRepoAllowRootRepo;
}

static bool supportsRootRepositoryPath(ForgePathMode mode)
{
    return mode == ForgePathMode::NestedOwnerRepoAllowRootRepo;
}

static ForgeRepoPath parseForgeRepoPath(
    const ParsedURL & url,
    const std::vector<std::string> & path,
    ForgePathMode mode,
    bool hasAuthority,
    bool hasQueryRefOrRev)
{
    if (path.size() < 2 && !(supportsRootRepositoryPath(mode) && hasAuthority && path.size() == 1))
        throw BadURL("URL '%s' is invalid", url);

    if (supportsRootRepositoryPath(mode) && hasAuthority && path.size() == 1)
        return ForgeRepoPath{.owner = "", .repo = path[0]};

    if (supportsNestedRepositoryPaths(mode) && (hasAuthority || hasQueryRefOrRev))
        return ForgeRepoPath{.owner = joinPathSegments(path.begin(), std::prev(path.end())), .repo = path.back()};

    return ForgeRepoPath{.owner = path[0], .repo = path[1]};
}

static std::vector<std::string>
renderForgeRepoPath(const std::string & owner, const std::string & repo, ForgePathMode mode)
{
    std::vector<std::string> repoPath;

    if (supportsNestedRepositoryPaths(mode)) {
        if (!owner.empty()) {
            auto ownerPath = splitPathSegments(owner);
            repoPath.insert(repoPath.end(), ownerPath.begin(), ownerPath.end());
        }
    } else {
        repoPath.push_back(owner);
    }

    repoPath.push_back(repo);
    return repoPath;
}

struct GitArchiveInputScheme : InputScheme
{
    virtual std::optional<std::pair<std::string, std::string>>
    accessHeaderFromToken(const std::string & token) const = 0;

    std::optional<Input>
    inputFromURL(const fetchers::Settings & settings, const ParsedURL & url, bool requireTree) const override
    {
        if (url.scheme != schemeName())
            return {};

        /* This ignores empty path segments for back-compat. Older versions used a tokenizeString here. */
        auto path = url.pathSegments(/*skipEmpty=*/true) | std::ranges::to<std::vector<std::string>>();

        Attrs attrs;
        bool hasAuthority = url.authority.has_value() && !url.authority->host.empty();
        bool hasQueryRefOrRev = false;

        if (hasAuthority) {
            if (!supportsURLAuthority())
                throw BadURL("URL authority is not supported by the '%s' input scheme", schemeName());

            auto authorityHost = authorityHostWithOptionalPort(*url.authority);
            auto defaultHostValue = defaultHost();
            if (!defaultHostValue || authorityHost != *defaultHostValue)
                attrs.insert_or_assign("host", authorityHost);
        }

        for (auto & [name, value] : url.query) {
            if (name == "rev") {
                if (attrs.contains(name))
                    throw BadURL("URL '%s' contains multiple commit hashes", url);
                attrs.insert_or_assign("rev", value);
                hasQueryRefOrRev = true;
            } else if (name == "ref") {
                if (attrs.contains(name))
                    throw BadURL("URL '%s' contains multiple branch/tag names", url);
                attrs.insert_or_assign("ref", value);
                hasQueryRefOrRev = true;
            } else if (name == "host") {
                if (!supportsURLAuthority())
                    throw BadURL("input scheme '%s' does not support custom hosts", schemeName());
                if (hasAuthority)
                    throw BadURL("URL '%s' specifies the host both as URL authority and as a query parameter", url);
                attrs.insert_or_assign("host", value);
            } else if (name == "narHash")
                attrs.insert_or_assign("narHash", value);
            else
                throw BadURL("URL '%s' contains unknown parameter '%s'", url, name);
        }

        /*
           The URL-authority form proposed in #6304 reserves path segments for
           the repository path. Branches/tags and revisions go in the query
           string. This matters for GitLab and cgit, where repository paths can
           be nested: gitlab://gitlab.example.org/org/group/repo?ref=main and
           cgit://git.example.org/pub/scm/project.git?ref=main.

           Keep the old path-ref shorthand for non-authority URLs without a
           ref/rev query parameter for backwards compatibility.
         */
        auto repoPath = parseForgeRepoPath(url, path, pathMode(), hasAuthority, hasQueryRefOrRev);
        attrs.insert_or_assign("owner", repoPath.owner);
        attrs.insert_or_assign("repo", repoPath.repo);

        if (!(supportsNestedRepositoryPaths(pathMode()) && (hasAuthority || hasQueryRefOrRev)) && path.size() > 2) {
            if (hasAuthority || hasQueryRefOrRev)
                throw BadURL(
                    "URL '%s' puts a branch/tag name or revision in the path; use '?ref=' or '?rev=' instead", url);

            if (path.size() == 3) {
                if (std::regex_match(path[2], revRegex))
                    attrs.insert_or_assign("rev", path[2]);
                else
                    attrs.insert_or_assign("ref", path[2]);
            } else {
                attrs.insert_or_assign("ref", joinPathSegments(std::next(path.begin(), 2), path.end()));
            }
        }

        attrs.insert_or_assign("type", std::string{schemeName()});

        return inputFromAttrs(settings, attrs);
    }

    virtual ForgePathMode pathMode() const
    {
        return ForgePathMode::OwnerRepo;
    }

    virtual RefSyntax refSyntax() const
    {
        return RefSyntax::QueryOnly;
    }

    virtual bool supportsURLAuthority() const
    {
        return true;
    }

    virtual std::optional<std::string> defaultHost() const = 0;

    const std::map<std::string, AttributeInfo> & allowedAttrs() const override
    {
        static const std::map<std::string, AttributeInfo> attrs = {
            {
                "owner",
                {},
            },
            {
                "repo",
                {},
            },
            {
                "ref",
                {},
            },
            {
                "rev",
                {},
            },
            {
                "narHash",
                {},
            },
            {
                "lastModified",
                {},
            },
            {
                "host",
                {},
            },
            {
                "treeHash",
                {},
            },
        };
        return attrs;
    }

    std::optional<Input> inputFromAttrs(const fetchers::Settings & settings, const Attrs & attrs) const override
    {
        getStrAttr(attrs, "owner");
        getStrAttr(attrs, "repo");

        auto ref = maybeGetStrAttr(attrs, "ref");
        auto rev = maybeGetStrAttr(attrs, "rev");
        if (ref && rev)
            throw BadURL(
                "input %s contains both a commit hash ('%s') and a branch/tag name ('%s')",
                attrsToJSON(attrs),
                *rev,
                *ref);

        if (rev)
            Hash::parseAny(*rev, HashAlgorithm::SHA1);

        if (ref && !isLegalRefName(*ref))
            throw BadURL("input %s contains an invalid branch/tag name", attrsToJSON(attrs));

        if (auto host = maybeGetStrAttr(attrs, "host")) {
            if (!supportsURLAuthority())
                throw BadURL("input scheme '%s' does not support custom hosts", schemeName());
            if (!std::regex_match(*host, hostRegex))
                throw BadURL("input %s contains an invalid instance host", attrsToJSON(attrs));
        } else if (!defaultHost()) {
            throw BadURL("input scheme '%s' requires a host", schemeName());
        }

        Input input{};
        input.attrs = attrs;
        return input;
    }

    ParsedURL toURL(const Input & input) const override
    {
        auto owner = getStrAttr(input.attrs, "owner");
        auto repo = getStrAttr(input.attrs, "repo");
        auto ref = input.getRef();
        auto rev = input.getRev();
        assert(!(ref && rev));

        auto repoPath = renderForgeRepoPath(owner, repo, pathMode());

        auto host = maybeGetStrAttr(input.attrs, "host");
        if (host && !supportsURLAuthority())
            throw BadURL("input scheme '%s' does not support custom hosts", schemeName());

        auto defaultHostValue = defaultHost();
        if (!host && !defaultHostValue)
            throw BadURL("input scheme '%s' requires a host", schemeName());

        auto ownerHasNestedPath = supportsNestedRepositoryPaths(pathMode()) && owner.find('/') != std::string::npos;
        auto useAuthority = supportsURLAuthority()
                            && ((host && (!defaultHostValue || *host != *defaultHostValue))
                                || (ownerHasNestedPath && !ref && !rev) || !defaultHostValue);
        auto authorityHost = host ? *host : *defaultHostValue;

        /* Preserve the historical canonical form for the original Git forge
           schemes when it is unambiguous: github:owner/repo/ref rather than
           github:owner/repo?ref=ref. New authority and nested-repository forms
           use query parameters because their path is reserved for the
           repository name. */
        auto useLegacyPathRef = refSyntax() == RefSyntax::LegacyPathAllowed && !useAuthority && !ownerHasNestedPath;

        std::vector<std::string> path;
        if (useAuthority)
            path.push_back("");
        path.insert(path.end(), repoPath.begin(), repoPath.end());
        if (useLegacyPathRef) {
            if (ref)
                path.push_back(*ref);
            if (rev)
                path.push_back(rev->to_string(HashFormat::Base16, false));
        }

        auto url = ParsedURL{
            .scheme = std::string{schemeName()},
            .authority = useAuthority ? std::optional<ParsedURL::Authority>{ParsedURL::Authority::parse(authorityHost)}
                                      : std::nullopt,
            .path = path,
        };
        if (!useLegacyPathRef) {
            if (ref)
                url.query.insert_or_assign("ref", *ref);
            if (rev)
                url.query.insert_or_assign("rev", rev->to_string(HashFormat::Base16, false));
        }
        if (auto narHash = input.getNarHash())
            url.query.insert_or_assign("narHash", narHash->to_string(HashFormat::SRI, true));
        return url;
    }

    Input applyOverrides(const Input & _input, std::optional<std::string> ref, std::optional<Hash> rev) const override
    {
        auto input(_input);
        if (rev && ref)
            throw BadURL(
                "cannot apply both a commit hash (%s) and a branch/tag name ('%s') to input '%s'",
                rev->gitRev(),
                *ref,
                input.to_string());
        if (rev) {
            input.attrs.insert_or_assign("rev", rev->gitRev());
            input.attrs.erase("ref");
        }
        if (ref) {
            input.attrs.insert_or_assign("ref", *ref);
            input.attrs.erase("rev");
        }
        return input;
    }

    // Search for the longest possible match starting from the beginning and ending at either the end or a path segment.
    std::optional<std::string> getAccessToken(
        const fetchers::Settings & settings, const std::string & host, const std::string & url) const override
    {
        auto tokens = settings.accessTokens.get();
        std::string answer;
        size_t answer_match_len = 0;
        if (!url.empty()) {
            for (auto & token : tokens) {
                auto first = url.find(token.first);
                if (first != std::string::npos && token.first.length() > answer_match_len && first == 0
                    && url.substr(0, token.first.length()) == token.first
                    && (url.length() == token.first.length() || url[token.first.length()] == '/')) {
                    answer = token.second;
                    answer_match_len = token.first.length();
                }
            }
            if (!answer.empty())
                return answer;
        }
        if (auto token = get(tokens, host))
            return *token;
        return {};
    }

    Headers
    makeHeadersWithAuthTokens(const fetchers::Settings & settings, const std::string & host, const Input & input) const
    {
        auto owner = getStrAttr(input.attrs, "owner");
        auto repo = getStrAttr(input.attrs, "repo");
        auto hostAndPath = owner.empty() ? fmt("%s/%s", host, repo) : fmt("%s/%s/%s", host, owner, repo);
        return makeHeadersWithAuthTokens(settings, host, hostAndPath);
    }

    Headers makeHeadersWithAuthTokens(
        const fetchers::Settings & settings, const std::string & host, const std::string & hostAndPath) const
    {
        Headers headers;
        auto accessToken = getAccessToken(settings, host, hostAndPath);
        if (accessToken) {
            auto hdr = accessHeaderFromToken(*accessToken);
            if (hdr)
                headers.push_back(*hdr);
            else
                warn("Unrecognized access token for host '%s'", host);
        }
        return headers;
    }

    struct RefInfo
    {
        Hash rev;
        std::optional<Hash> treeHash;
    };

    virtual RefInfo getRevFromRef(const Settings & settings, nix::Store & store, const Input & input) const = 0;

    virtual DownloadUrl getDownloadUrl(const Settings & settings, const Input & input) const = 0;

    struct TarballInfo
    {
        Hash treeHash;
        time_t lastModified;
    };

    std::pair<Input, TarballInfo> downloadArchive(const Settings & settings, Store & store, Input input) const
    {
        if (!maybeGetStrAttr(input.attrs, "ref"))
            input.attrs.insert_or_assign("ref", "HEAD");

        std::optional<Hash> upstreamTreeHash;

        auto rev = input.getRev();
        if (!rev) {
            auto refInfo = getRevFromRef(settings, store, input);
            rev = refInfo.rev;
            upstreamTreeHash = refInfo.treeHash;
            debug("HEAD revision for '%s' is %s", input.to_string(), refInfo.rev.gitRev());
        }

        input.attrs.erase("ref");
        input.attrs.insert_or_assign("rev", rev->gitRev());

        auto cache = settings.getCache();

        Cache::Key treeHashKey{"gitRevToTreeHash", {{"rev", rev->gitRev()}}};
        Cache::Key lastModifiedKey{"gitRevToLastModified", {{"rev", rev->gitRev()}}};

        if (auto treeHashAttrs = cache->lookup(treeHashKey)) {
            if (auto lastModifiedAttrs = cache->lookup(lastModifiedKey)) {
                auto treeHash = getRevAttr(*treeHashAttrs, "treeHash");
                auto lastModified = getIntAttr(*lastModifiedAttrs, "lastModified");
                if (settings.getTarballCache()->hasObject(treeHash))
                    return {std::move(input), TarballInfo{.treeHash = treeHash, .lastModified = (time_t) lastModified}};
                else
                    debug("Git tree with hash '%s' has disappeared from the cache, refetching...", treeHash.gitRev());
            }
        }

        /* Stream the tarball into the tarball cache. */
        auto url = getDownloadUrl(settings, input);

        auto source = sinkToSource([&](Sink & sink) {
            FileTransferRequest req(url.url);
            req.headers = url.headers;
            getFileTransfer()->download(std::move(req), sink);
        });

        auto act = std::make_unique<Activity>(
            *logger, lvlInfo, actUnknown, fmt("unpacking '%s' into the Git cache", input.to_string()));

        TarArchive archive{*source};
        auto tarballCache = settings.getTarballCache();
        auto parseSink = tarballCache->getFileSystemObjectSink();
        auto lastModified = unpackTarfileToSink(archive, *parseSink);
        auto tree = parseSink->flush();

        act.reset();

        TarballInfo tarballInfo{
            .treeHash = tarballCache->dereferenceSingletonDirectory(tree), .lastModified = lastModified};

        cache->upsert(treeHashKey, Attrs{{"treeHash", tarballInfo.treeHash.gitRev()}});
        cache->upsert(lastModifiedKey, Attrs{{"lastModified", (uint64_t) tarballInfo.lastModified}});

#if 0
        if (upstreamTreeHash != tarballInfo.treeHash)
            warn(
                "Git tree hash mismatch for revision '%s' of '%s': "
                "expected '%s', got '%s'. "
                "This can happen if the Git repository uses submodules.",
                rev->gitRev(), input.to_string(), upstreamTreeHash->gitRev(), tarballInfo.treeHash.gitRev());
#endif

        return {std::move(input), tarballInfo};
    }

    std::pair<ref<SourceAccessor>, Input>
    getAccessor(const Settings & settings, Store & store, const Input & _input) const override
    {
        auto [input, tarballInfo] = downloadArchive(settings, store, _input);

#if 0
        input.attrs.insert_or_assign("treeHash", tarballInfo.treeHash.gitRev());
#endif
        input.attrs.insert_or_assign("lastModified", uint64_t(tarballInfo.lastModified));

        auto accessor =
            settings.getTarballCache()->getAccessor(tarballInfo.treeHash, {}, "«" + input.to_string() + "»");

        return {accessor, input};
    }

    bool isLocked(const Settings & settings, const Input & input) const override
    {
        /* Since we can't verify the integrity of the tarball from the
           Git revision alone, we also require a NAR hash for
           locking. FIXME: in the future, we may want to require a Git
           tree hash instead of a NAR hash. */
        return input.getRev().has_value() && (settings.trustTarballsFromGitForges || input.getNarHash().has_value());
    }

    std::optional<ExperimentalFeature> experimentalFeature() const override
    {
        return Xp::Flakes;
    }

    std::optional<std::string> getFingerprint(Store & store, const Input & input) const override
    {
        if (auto rev = input.getRev())
            return rev->gitRev();
        else
            return std::nullopt;
    }
};

enum class GitForgeScheme {
    GitHub,
    GitLab,
    SourceHut,
    Gitea,
    CGit,
    Bitbucket,
};

struct GitForgeSchemeInstance
{
    std::string scheme;
    GitForgeScheme forge;
    std::optional<std::string> host;
    bool supportsAuthority = true;
    ForgePathMode pathMode = ForgePathMode::OwnerRepo;
    RefSyntax refSyntax = RefSyntax::QueryOnly;
};

struct GitForgeSchemeInfo
{
    GitForgeScheme scheme;
    std::string name;
    bool cloneUrlNeedsDotGit;
};

static const std::vector<GitForgeSchemeInfo> & gitForgeSchemeInfos()
{
    static const std::vector<GitForgeSchemeInfo> infos = {
        GitForgeSchemeInfo{
            .scheme = GitForgeScheme::GitHub,
            .name = "github",
            .cloneUrlNeedsDotGit = true,
        },
        GitForgeSchemeInfo{
            .scheme = GitForgeScheme::GitLab,
            .name = "gitlab",
            .cloneUrlNeedsDotGit = true,
        },
        GitForgeSchemeInfo{
            .scheme = GitForgeScheme::SourceHut,
            .name = "sourcehut",
            .cloneUrlNeedsDotGit = false,
        },
        GitForgeSchemeInfo{
            .scheme = GitForgeScheme::Gitea,
            .name = "gitea/forgejo",
            .cloneUrlNeedsDotGit = true,
        },
        GitForgeSchemeInfo{
            .scheme = GitForgeScheme::CGit,
            .name = "cgit",
            .cloneUrlNeedsDotGit = false,
        },
        GitForgeSchemeInfo{
            .scheme = GitForgeScheme::Bitbucket,
            .name = "bitbucket",
            .cloneUrlNeedsDotGit = true,
        },
    };
    return infos;
}

static const GitForgeSchemeInfo & gitForgeSchemeInfo(GitForgeScheme forge)
{
    for (auto & info : gitForgeSchemeInfos())
        if (info.scheme == forge)
            return info;
    unreachable();
}

static std::string showGitForgeScheme(GitForgeScheme forge)
{
    return gitForgeSchemeInfo(forge).name;
}

struct GitForgeInputScheme : GitArchiveInputScheme
{
    GitForgeSchemeInstance config;

    explicit GitForgeInputScheme(GitForgeSchemeInstance config)
        : config(std::move(config))
    {
    }

    std::string_view schemeName() const override
    {
        return config.scheme;
    }

    std::string schemeDescription() const override
    {
        if (config.host)
            return fmt(
                "fetch %s repositories from %s as tarball archives", showGitForgeScheme(config.forge), *config.host);

        return fmt(
            "fetch %s repositories from a URL authority host as tarball archives", showGitForgeScheme(config.forge));
    }

    ForgePathMode pathMode() const override
    {
        return config.pathMode;
    }

    RefSyntax refSyntax() const override
    {
        return config.refSyntax;
    }

    bool supportsURLAuthority() const override
    {
        return config.supportsAuthority;
    }

    std::optional<std::string> defaultHost() const override
    {
        return config.host;
    }

    std::string getHost(const Input & input) const
    {
        if (auto host = maybeGetStrAttr(input.attrs, "host"))
            return *host;
        if (config.host)
            return *config.host;
        throw BadURL("input scheme '%s' requires a host", schemeName());
    }

    std::string getOwner(const Input & input) const
    {
        return getStrAttr(input.attrs, "owner");
    }

    std::string getRepo(const Input & input) const
    {
        return getStrAttr(input.attrs, "repo");
    }

    struct RepoContext
    {
        std::string host;
        std::string owner;
        std::string repo;
        Headers headers;
    };

    RepoContext getRepoContext(const Settings & settings, const Input & input) const
    {
        auto host = getHost(input);
        return RepoContext{
            .host = host,
            .owner = getOwner(input),
            .repo = getRepo(input),
            .headers = makeHeadersWithAuthTokens(settings, host, input),
        };
    }

    static std::string repoPathForUrl(const RepoContext & ctx)
    {
        return ctx.owner.empty() ? ctx.repo : fmt("%s/%s", ctx.owner, ctx.repo);
    }

    std::string revString(const Input & input) const
    {
        return input.getRev()->to_string(HashFormat::Base16, false);
    }

    std::optional<std::pair<std::string, std::string>> accessHeaderFromToken(const std::string & token) const override
    {
        switch (config.forge) {
        case GitForgeScheme::GitHub:
            // GitHub supports PAT/OAuth2 tokens and HTTP Basic Authentication.
            return std::pair<std::string, std::string>("Authorization", fmt("token %s", token));
        case GitForgeScheme::GitLab: {
            // GitLab supports 4 kinds of authorization, two of which are
            // relevant here: OAuth2 and PAT (Private Access Token).  The user
            // can indicate which token is used by specifying the token as
            // <TYPE>:<VALUE>, where type is "OAuth2" or "PAT". If the <TYPE>
            // is unrecognized, this falls back to treating it as
            // <HDRNAME>:<HDRVAL>.
            auto fldsplit = token.find_first_of(':');
            if ("OAuth2" == token.substr(0, fldsplit))
                return std::make_pair("Authorization", fmt("Bearer %s", token.substr(fldsplit + 1)));
            if ("PAT" == token.substr(0, fldsplit))
                return std::make_pair("Private-token", token.substr(fldsplit + 1));
            warn("Unrecognized GitLab token type %s", token.substr(0, fldsplit));
            return std::make_pair(token.substr(0, fldsplit), token.substr(fldsplit + 1));
        }
        case GitForgeScheme::SourceHut:
            // Note: this currently serves no purpose for private SourceHut repos
            // because SourceHut does not allow token-authenticated tarball downloads.
            return std::pair<std::string, std::string>("Authorization", fmt("Bearer %s", token));
        case GitForgeScheme::Gitea:
            return std::pair<std::string, std::string>("Authorization", fmt("token %s", token));
        case GitForgeScheme::CGit: {
            auto fldsplit = token.find_first_of(':');
            if (fldsplit != std::string::npos)
                return std::make_pair(token.substr(0, fldsplit), token.substr(fldsplit + 1));
            return {};
        }
        case GitForgeScheme::Bitbucket: {
            // Bitbucket Cloud supports OAuth/access-token authentication with
            // Bearer tokens. Users that need app-password Basic auth can pass
            // a complete header value as "Basic:<base64>".
            auto fldsplit = token.find_first_of(':');
            if (fldsplit != std::string::npos && token.substr(0, fldsplit) == "Basic")
                return std::make_pair("Authorization", fmt("Basic %s", token.substr(fldsplit + 1)));
            if (fldsplit != std::string::npos && token.substr(0, fldsplit) == "Bearer")
                return std::make_pair("Authorization", fmt("Bearer %s", token.substr(fldsplit + 1)));
            return std::make_pair("Authorization", fmt("Bearer %s", token));
        }
        }
        unreachable();
    }

    RefInfo getRevFromRef(const Settings & settings, nix::Store & store, const Input & input) const override
    {
        switch (config.forge) {
        case GitForgeScheme::GitHub:
            return getGitHubRevFromRef(settings, store, input);
        case GitForgeScheme::GitLab:
            return getGitLabRevFromRef(settings, store, input);
        case GitForgeScheme::SourceHut:
            return getSourceHutRevFromRef(settings, store, input);
        case GitForgeScheme::Gitea:
            return getGiteaRevFromRef(settings, store, input);
        case GitForgeScheme::CGit:
            return getCGitRevFromRef(settings, store, input);
        case GitForgeScheme::Bitbucket:
            return getBitbucketRevFromRef(settings, store, input);
        }
        unreachable();
    }

    DownloadUrl getDownloadUrl(const Settings & settings, const Input & input) const override
    {
        switch (config.forge) {
        case GitForgeScheme::GitHub:
            return getGitHubDownloadUrl(settings, input);
        case GitForgeScheme::GitLab:
            return getGitLabDownloadUrl(settings, input);
        case GitForgeScheme::SourceHut:
            return getSourceHutDownloadUrl(settings, input);
        case GitForgeScheme::Gitea:
            return getGiteaDownloadUrl(settings, input);
        case GitForgeScheme::CGit:
            return getCGitDownloadUrl(settings, input);
        case GitForgeScheme::Bitbucket:
            return getBitbucketDownloadUrl(settings, input);
        }
        unreachable();
    }

    RefInfo getGitHubRevFromRef(const Settings & settings, nix::Store & store, const Input & input) const
    {
        auto ctx = getRepoContext(settings, input);
        auto url =
            fmt(ctx.host == "github.com" ? "https://api.%s/repos/%s/%s/commits/%s"
                                         : "https://%s/api/v3/repos/%s/%s/commits/%s",
                ctx.host,
                ctx.owner,
                ctx.repo,
                *input.getRef());

        auto json = downloadJSON(store, settings, url, ctx.headers);

        return RefInfo{
            .rev = Hash::parseAny(std::string{json["sha"]}, HashAlgorithm::SHA1),
            .treeHash = Hash::parseAny(std::string{json["commit"]["tree"]["sha"]}, HashAlgorithm::SHA1)};
    }

    RefInfo getGitLabRevFromRef(const Settings & settings, nix::Store & store, const Input & input) const
    {
        auto ctx = getRepoContext(settings, input);
        // See rate limiting note below.
        auto url =
            fmt("https://%s/api/v4/projects/%s/repository/commits?ref_name=%s",
                ctx.host,
                percentEncode(ctx.owner + "/" + ctx.repo),
                percentEncode(*input.getRef()));

        auto json = downloadJSON(store, settings, url, ctx.headers);

        if (json.is_array() && json.size() >= 1 && json[0]["id"] != nullptr) {
            return RefInfo{.rev = Hash::parseAny(std::string(json[0]["id"]), HashAlgorithm::SHA1)};
        }
        if (json.is_array() && json.size() == 0) {
            throw Error("No commits returned by GitLab API -- does the git ref really exist?");
        } else {
            throw Error("Unexpected response received from GitLab: %s", json);
        }
    }

    RefInfo getSourceHutRevFromRef(const Settings & settings, nix::Store & store, const Input & input) const
    {
        // TODO: In the future, when the sourcehut graphql API is implemented for mercurial
        // and with anonymous access, this method should use it instead.

        auto ref = *input.getRef();
        auto ctx = getRepoContext(settings, input);
        auto base_url = fmt("https://%s/%s", ctx.host, repoPathForUrl(ctx));

        std::string refUri;
        if (ref == "HEAD") {
            auto downloadFileResult = downloadFile(store, settings, fmt("%s/HEAD", base_url), "source", ctx.headers);
            auto contents = store.requireStoreObjectAccessor(downloadFileResult.storePath)->readFile(CanonPath::root);

            auto remoteLine = git::parseLsRemoteLine(getLine(contents).first);
            if (!remoteLine) {
                throw BadURL("in '%s', couldn't resolve HEAD ref '%s'", input.to_string(), ref);
            }
            refUri = remoteLine->target;
        } else {
            refUri = fmt("refs/(heads|tags)/%s", ref);
        }
        std::regex refRegex(refUri);

        auto downloadFileResult = downloadFile(store, settings, fmt("%s/info/refs", base_url), "source", ctx.headers);
        auto contents = store.requireStoreObjectAccessor(downloadFileResult.storePath)->readFile(CanonPath::root);
        std::istringstream is(contents);

        std::string line;
        std::optional<std::string> id;
        while (!id && getline(is, line)) {
            auto parsedLine = git::parseLsRemoteLine(line);
            if (parsedLine && parsedLine->reference && std::regex_match(*parsedLine->reference, refRegex))
                id = parsedLine->target;
        }

        if (!id)
            throw BadURL("in '%s', couldn't find ref '%s'", input.to_string(), ref);

        return RefInfo{.rev = Hash::parseAny(*id, HashAlgorithm::SHA1)};
    }

    RefInfo getCGitRevFromRef(const Settings & settings, nix::Store & store, const Input & input) const
    {
        return getSourceHutRevFromRef(settings, store, input);
    }

    RefInfo getGiteaRevFromRef(const Settings & settings, nix::Store & store, const Input & input) const
    {
        auto ctx = getRepoContext(settings, input);
        auto ref = *input.getRef();

        if (ref == "HEAD") {
            auto repoJson = downloadJSON(
                store, settings, fmt("https://%s/api/v1/repos/%s/%s", ctx.host, ctx.owner, ctx.repo), ctx.headers);

            auto defaultBranch = repoJson.find("default_branch");
            if (defaultBranch == repoJson.end() || !defaultBranch->is_string()
                || defaultBranch->get<std::string>().empty())
                throw Error("Gitea API response for '%s/%s' did not include a default branch", ctx.owner, ctx.repo);

            ref = defaultBranch->get<std::string>();
        }

        auto json = downloadJSON(
            store,
            settings,
            fmt("https://%s/api/v1/repos/%s/%s/commits?sha=%s&limit=1",
                ctx.host,
                ctx.owner,
                ctx.repo,
                percentEncode(ref)),
            ctx.headers);

        if (!json.is_array() || json.empty() || !json[0].contains("sha"))
            throw Error("No commits returned by Gitea API for ref '%s' in '%s/%s'", ref, ctx.owner, ctx.repo);

        std::optional<Hash> treeHash;
        if (json[0].contains("commit") && json[0]["commit"].contains("tree")
            && json[0]["commit"]["tree"].contains("sha"))
            treeHash = Hash::parseAny(std::string{json[0]["commit"]["tree"]["sha"]}, HashAlgorithm::SHA1);

        return RefInfo{.rev = Hash::parseAny(std::string{json[0]["sha"]}, HashAlgorithm::SHA1), .treeHash = treeHash};
    }

    RefInfo getBitbucketRevFromRef(const Settings & settings, nix::Store & store, const Input & input) const
    {
        auto ctx = getRepoContext(settings, input);
        auto ref = *input.getRef();

        auto apiHost = ctx.host == "bitbucket.org" ? "api.bitbucket.org" : ctx.host;

        if (ref == "HEAD") {
            auto repoJson = downloadJSON(
                store, settings, fmt("https://%s/2.0/repositories/%s/%s", apiHost, ctx.owner, ctx.repo), ctx.headers);

            if (!repoJson.contains("mainbranch") || !repoJson["mainbranch"].contains("name")
                || !repoJson["mainbranch"]["name"].is_string()
                || repoJson["mainbranch"]["name"].get<std::string>().empty())
                throw Error("Bitbucket API response for '%s/%s' did not include a main branch", ctx.owner, ctx.repo);

            ref = repoJson["mainbranch"]["name"].get<std::string>();
        }

        auto json = downloadJSON(
            store,
            settings,
            fmt("https://%s/2.0/repositories/%s/%s/commits/%s?pagelen=1",
                apiHost,
                ctx.owner,
                ctx.repo,
                percentEncode(ref)),
            ctx.headers);

        if (!json.contains("values") || !json["values"].is_array() || json["values"].empty()
            || !json["values"][0].contains("hash"))
            throw Error("No commits returned by Bitbucket API for ref '%s' in '%s/%s'", ref, ctx.owner, ctx.repo);

        return RefInfo{.rev = Hash::parseAny(std::string{json["values"][0]["hash"]}, HashAlgorithm::SHA1)};
    }

    DownloadUrl getGitHubDownloadUrl(const Settings & settings, const Input & input) const
    {
        auto ctx = getRepoContext(settings, input);

        // If we have no auth headers then we default to the public archive
        // URLs so we do not run into rate limits.
        const auto urlFmt = ctx.host != "github.com" ? "https://%s/api/v3/repos/%s/%s/tarball/%s"
                            : ctx.headers.empty()    ? "https://%s/%s/%s/archive/%s.tar.gz"
                                                     : "https://api.%s/repos/%s/%s/tarball/%s";

        const auto url = fmt(urlFmt, ctx.host, ctx.owner, ctx.repo, revString(input));

        return DownloadUrl{parseURL(url), ctx.headers};
    }

    DownloadUrl getGitLabDownloadUrl(const Settings & settings, const Input & input) const
    {
        // This endpoint has a rate limit threshold that may be server-specific
        // and vary based whether the user is authenticated via an accessToken
        // or not, but the usual rate is 10 reqs/sec/ip-addr. See
        // https://docs.gitlab.com/ee/user/gitlab_com/index.html#gitlabcom-specific-rate-limits
        auto ctx = getRepoContext(settings, input);
        auto url =
            fmt("https://%s/api/v4/projects/%s/repository/archive.tar.gz?sha=%s",
                ctx.host,
                percentEncode(ctx.owner + "/" + ctx.repo),
                revString(input));

        return DownloadUrl{parseURL(url), ctx.headers};
    }

    DownloadUrl getArchiveDotTarGzDownloadUrl(const Settings & settings, const Input & input) const
    {
        auto ctx = getRepoContext(settings, input);
        auto url = fmt("https://%s/%s/%s/archive/%s.tar.gz", ctx.host, ctx.owner, ctx.repo, revString(input));

        return DownloadUrl{parseURL(url), ctx.headers};
    }

    DownloadUrl getSourceHutDownloadUrl(const Settings & settings, const Input & input) const
    {
        return getArchiveDotTarGzDownloadUrl(settings, input);
    }

    DownloadUrl getGiteaDownloadUrl(const Settings & settings, const Input & input) const
    {
        return getArchiveDotTarGzDownloadUrl(settings, input);
    }

    static std::string cgitSnapshotBasename(const std::string & repo)
    {
        if (repo.size() >= 4 && repo.compare(repo.size() - 4, 4, ".git") == 0)
            return repo.substr(0, repo.size() - 4);
        return repo;
    }

    DownloadUrl getCGitDownloadUrl(const Settings & settings, const Input & input) const
    {
        auto ctx = getRepoContext(settings, input);
        auto url =
            fmt("https://%s/%s/snapshot/%s-%s.tar.gz",
                ctx.host,
                repoPathForUrl(ctx),
                cgitSnapshotBasename(ctx.repo),
                revString(input));

        return DownloadUrl{parseURL(url), ctx.headers};
    }

    DownloadUrl getBitbucketDownloadUrl(const Settings & settings, const Input & input) const
    {
        auto ctx = getRepoContext(settings, input);
        auto url = fmt("https://%s/%s/%s/get/%s.zip", ctx.host, ctx.owner, ctx.repo, revString(input));

        return DownloadUrl{parseURL(url), ctx.headers};
    }

    void clone(const Settings & settings, Store & store, const Input & input, const std::filesystem::path & destDir)
        const override
    {
        auto ctx = getRepoContext(settings, input);
        auto cloneRepo = gitForgeSchemeInfo(config.forge).cloneUrlNeedsDotGit ? fmt("%s.git", ctx.repo) : ctx.repo;
        auto clonePath = ctx.owner.empty() ? cloneRepo : fmt("%s/%s", ctx.owner, cloneRepo);
        auto cloneUrl = fmt("git+https://%s/%s", ctx.host, clonePath);

        Input::fromURL(settings, cloneUrl)
            .applyOverrides(input.getRef(), input.getRev())
            .clone(settings, store, destDir);
    }
};

static std::vector<GitForgeSchemeInstance> defaultGitForgeInputSchemeConfigs()
{
    return {
        GitForgeSchemeInstance{
            .scheme = "github",
            .forge = GitForgeScheme::GitHub,
            .host = "github.com",
            .refSyntax = RefSyntax::LegacyPathAllowed,
        },
        GitForgeSchemeInstance{
            .scheme = "gitlab",
            .forge = GitForgeScheme::GitLab,
            .host = "gitlab.com",
            .pathMode = ForgePathMode::NestedOwnerRepo,
            .refSyntax = RefSyntax::LegacyPathAllowed,
        },
        GitForgeSchemeInstance{
            .scheme = "sourcehut",
            .forge = GitForgeScheme::SourceHut,
            .host = "git.sr.ht",
            .refSyntax = RefSyntax::LegacyPathAllowed,
        },
        GitForgeSchemeInstance{
            .scheme = "codeberg",
            .forge = GitForgeScheme::Gitea,
            .host = "codeberg.org",
            .supportsAuthority = false,
        },
        GitForgeSchemeInstance{.scheme = "gitea", .forge = GitForgeScheme::Gitea},
        GitForgeSchemeInstance{.scheme = "forgejo", .forge = GitForgeScheme::Gitea},
        GitForgeSchemeInstance{
            .scheme = "cgit",
            .forge = GitForgeScheme::CGit,
            .pathMode = ForgePathMode::NestedOwnerRepoAllowRootRepo,
        },
        GitForgeSchemeInstance{.scheme = "bitbucket", .forge = GitForgeScheme::Bitbucket, .host = "bitbucket.org"},
    };
}

static void registerDefaultGitForgeInputSchemes()
{
    for (auto & config : defaultGitForgeInputSchemeConfigs())
        registerInputScheme(std::make_shared<GitForgeInputScheme>(std::move(config)));
}

static auto rGitForgeInputSchemes = OnStartup([] { registerDefaultGitForgeInputSchemes(); });

} // namespace nix::fetchers
