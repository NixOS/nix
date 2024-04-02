#include "filetransfer.hh"
#include "cache.hh"
#include "globals.hh"
#include "store-api.hh"
#include "types.hh"
#include "url-parts.hh"
#include "git.hh"
#include "fetchers.hh"
#include "fetch-settings.hh"
#include "tarball.hh"
#include "tarfile.hh"
#include "git-utils.hh"

#include <optional>
#include <nlohmann/json.hpp>
#include <fstream>

namespace nix::fetchers {

struct DownloadUrl
{
    std::string url;
    Headers headers;
};

// A github, gitlab, or sourcehut host
const static std::string hostRegexS = "[a-zA-Z0-9.-]*"; // FIXME: check
std::regex hostRegex(hostRegexS, std::regex::ECMAScript);

struct GitArchiveInputScheme : InputScheme
{
    virtual std::optional<std::pair<std::string, std::string>> accessHeaderFromToken(const std::string & token) const = 0;

    std::optional<Input> inputFromURL(const ParsedURL & url, bool requireTree) const override
    {
        if (url.scheme != schemeName()) return {};

        auto path = tokenizeString<std::vector<std::string>>(url.path, "/");

        std::optional<Hash> rev;
        std::optional<std::string> ref;
        std::optional<std::string> host_url;

        auto size = path.size();
        if (size == 3) {
            if (std::regex_match(path[2], revRegex))
                rev = Hash::parseAny(path[2], HashAlgorithm::SHA1);
            else if (std::regex_match(path[2], refRegex))
                ref = path[2];
            else
                throw BadURL("in URL '%s', '%s' is not a commit hash or branch/tag name", url.url, path[2]);
        } else if (size > 3) {
            std::string rs;
            for (auto i = std::next(path.begin(), 2); i != path.end(); i++) {
                rs += *i;
                if (std::next(i) != path.end()) {
                    rs += "/";
                }
            }

            if (std::regex_match(rs, refRegex)) {
                ref = rs;
            } else {
                throw BadURL("in URL '%s', '%s' is not a branch/tag name", url.url, rs);
            }
        } else if (size < 2)
            throw BadURL("URL '%s' is invalid", url.url);

        for (auto &[name, value] : url.query) {
            if (name == "rev") {
                if (rev)
                    throw BadURL("URL '%s' contains multiple commit hashes", url.url);
                rev = Hash::parseAny(value, HashAlgorithm::SHA1);
            }
            else if (name == "ref") {
                if (!std::regex_match(value, refRegex))
                    throw BadURL("URL '%s' contains an invalid branch/tag name", url.url);
                if (ref)
                    throw BadURL("URL '%s' contains multiple branch/tag names", url.url);
                ref = value;
            }
            else if (name == "host") {
                if (!std::regex_match(value, hostRegex))
                    throw BadURL("URL '%s' contains an invalid instance host", url.url);
                host_url = value;
            }
            // FIXME: barf on unsupported attributes
        }

        if (ref && rev)
            throw BadURL("URL '%s' contains both a commit hash and a branch/tag name %s %s", url.url, *ref, rev->gitRev());

        Input input;
        input.attrs.insert_or_assign("type", std::string { schemeName() });
        input.attrs.insert_or_assign("owner", path[0]);
        input.attrs.insert_or_assign("repo", path[1]);
        if (rev) input.attrs.insert_or_assign("rev", rev->gitRev());
        if (ref) input.attrs.insert_or_assign("ref", *ref);
        if (host_url) input.attrs.insert_or_assign("host", *host_url);

        auto narHash = url.query.find("narHash");
        if (narHash != url.query.end())
            input.attrs.insert_or_assign("narHash", narHash->second);

        return input;
    }

    StringSet allowedAttrs() const override
    {
        return {
            "owner",
            "repo",
            "ref",
            "rev",
            "narHash",
            "lastModified",
            "host",
            "treeHash",
        };
    }

    std::optional<Input> inputFromAttrs(const Attrs & attrs) const override
    {
        getStrAttr(attrs, "owner");
        getStrAttr(attrs, "repo");

        Input input;
        input.attrs = attrs;
        return input;
    }

    ParsedURL toURL(const Input & input) const override
    {
        auto owner = getStrAttr(input.attrs, "owner");
        auto repo = getStrAttr(input.attrs, "repo");
        auto ref = input.getRef();
        auto rev = input.getRev();
        auto path = owner + "/" + repo;
        assert(!(ref && rev));
        if (ref) path += "/" + *ref;
        if (rev) path += "/" + rev->to_string(HashFormat::Base16, false);
        auto url = ParsedURL {
            .scheme = std::string { schemeName() },
            .path = path,
        };
        if (auto narHash = input.getNarHash())
            url.query.insert_or_assign("narHash", narHash->to_string(HashFormat::SRI, true));
        return url;
    }

    Input applyOverrides(
        const Input & _input,
        std::optional<std::string> ref,
        std::optional<Hash> rev) const override
    {
        auto input(_input);
        if (rev && ref)
            throw BadURL("cannot apply both a commit hash (%s) and a branch/tag name ('%s') to input '%s'",
                rev->gitRev(), *ref, input.to_string());
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

    std::optional<std::string> getAccessToken(const std::string & host) const
    {
        auto tokens = fetchSettings.accessTokens.get();
        if (auto token = get(tokens, host))
            return *token;
        return {};
    }

    Headers makeHeadersWithAuthTokens(const std::string & host) const
    {
        Headers headers;
        auto accessToken = getAccessToken(host);
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

    virtual RefInfo getRevFromRef(nix::ref<Store> store, const Input & input) const = 0;

    virtual DownloadUrl getDownloadUrl(const Input & input) const = 0;

    struct TarballInfo
    {
        Hash treeHash;
        time_t lastModified;
    };

    std::pair<Input, TarballInfo> downloadArchive(ref<Store> store, Input input) const
    {
        if (!maybeGetStrAttr(input.attrs, "ref")) input.attrs.insert_or_assign("ref", "HEAD");

        std::optional<Hash> upstreamTreeHash;

        auto rev = input.getRev();
        if (!rev) {
            auto refInfo = getRevFromRef(store, input);
            rev = refInfo.rev;
            upstreamTreeHash = refInfo.treeHash;
            debug("HEAD revision for '%s' is %s", input.to_string(), refInfo.rev.gitRev());
        }

        input.attrs.erase("ref");
        input.attrs.insert_or_assign("rev", rev->gitRev());

        auto cache = getCache();

        Attrs treeHashKey{{"_what", "gitRevToTreeHash"}, {"rev", rev->gitRev()}};
        Attrs lastModifiedKey{{"_what", "gitRevToLastModified"}, {"rev", rev->gitRev()}};

        if (auto treeHashAttrs = cache->lookup(treeHashKey)) {
            if (auto lastModifiedAttrs = cache->lookup(lastModifiedKey)) {
                auto treeHash = getRevAttr(*treeHashAttrs, "treeHash");
                auto lastModified = getIntAttr(*lastModifiedAttrs, "lastModified");
                if (getTarballCache()->hasObject(treeHash))
                    return {std::move(input), TarballInfo { .treeHash = treeHash, .lastModified = (time_t) lastModified }};
                else
                    debug("Git tree with hash '%s' has disappeared from the cache, refetching...", treeHash.gitRev());
            }
        }

        /* Stream the tarball into the tarball cache. */
        auto url = getDownloadUrl(input);

        auto source = sinkToSource([&](Sink & sink) {
            FileTransferRequest req(url.url);
            req.headers = url.headers;
            getFileTransfer()->download(std::move(req), sink);
        });

        TarArchive archive { *source };
        auto parseSink = getTarballCache()->getFileSystemObjectSink();
        auto lastModified = unpackTarfileToSink(archive, *parseSink);

        TarballInfo tarballInfo {
            .treeHash = parseSink->sync(),
            .lastModified = lastModified
        };

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

    std::pair<ref<InputAccessor>, Input> getAccessor(ref<Store> store, const Input & _input) const override
    {
        auto [input, tarballInfo] = downloadArchive(store, _input);

        #if 0
        input.attrs.insert_or_assign("treeHash", tarballInfo.treeHash.gitRev());
        #endif
        input.attrs.insert_or_assign("lastModified", uint64_t(tarballInfo.lastModified));

        auto accessor = getTarballCache()->getAccessor(tarballInfo.treeHash, false);

        accessor->setPathDisplay("«" + input.to_string() + "»");

        return {accessor, input};
    }

    bool isLocked(const Input & input) const override
    {
        /* Since we can't verify the integrity of the tarball from the
           Git revision alone, we also require a NAR hash for
           locking. FIXME: in the future, we may want to require a Git
           tree hash instead of a NAR hash. */
        return input.getRev().has_value()
            && (fetchSettings.trustTarballsFromGitForges ||
                input.getNarHash().has_value());
    }

    std::optional<ExperimentalFeature> experimentalFeature() const override
    {
        return Xp::Flakes;
    }

    std::optional<std::string> getFingerprint(ref<Store> store, const Input & input) const override
    {
        if (auto rev = input.getRev())
            return rev->gitRev();
        else
            return std::nullopt;
    }
};

struct GitHubInputScheme : GitArchiveInputScheme
{
    std::string_view schemeName() const override { return "github"; }

    std::optional<std::pair<std::string, std::string>> accessHeaderFromToken(const std::string & token) const override
    {
        // Github supports PAT/OAuth2 tokens and HTTP Basic
        // Authentication.  The former simply specifies the token, the
        // latter can use the token as the password.  Only the first
        // is used here. See
        // https://developer.github.com/v3/#authentication and
        // https://docs.github.com/en/developers/apps/authorizing-oath-apps
        return std::pair<std::string, std::string>("Authorization", fmt("token %s", token));
    }

    std::string getHost(const Input & input) const
    {
        return maybeGetStrAttr(input.attrs, "host").value_or("github.com");
    }

    std::string getOwner(const Input & input) const
    {
        return getStrAttr(input.attrs, "owner");
    }

    std::string getRepo(const Input & input) const
    {
        return getStrAttr(input.attrs, "repo");
    }

    RefInfo getRevFromRef(nix::ref<Store> store, const Input & input) const override
    {
        auto host = getHost(input);
        auto url = fmt(
            host == "github.com"
            ? "https://api.%s/repos/%s/%s/commits/%s"
            : "https://%s/api/v3/repos/%s/%s/commits/%s",
            host, getOwner(input), getRepo(input), *input.getRef());

        Headers headers = makeHeadersWithAuthTokens(host);

        auto json = nlohmann::json::parse(
            readFile(
                store->toRealPath(
                    downloadFile(store, url, "source", false, headers).storePath)));

        return RefInfo {
            .rev = Hash::parseAny(std::string { json["sha"] }, HashAlgorithm::SHA1),
            .treeHash = Hash::parseAny(std::string { json["commit"]["tree"]["sha"] }, HashAlgorithm::SHA1)
        };
    }

    DownloadUrl getDownloadUrl(const Input & input) const override
    {
        auto host = getHost(input);

        Headers headers = makeHeadersWithAuthTokens(host);

        // If we have no auth headers then we default to the public archive
        // urls so we do not run into rate limits.
        const auto urlFmt =
            host != "github.com"
            ? "https://%s/api/v3/repos/%s/%s/tarball/%s"
            : headers.empty()
            ? "https://%s/%s/%s/archive/%s.tar.gz"
            : "https://api.%s/repos/%s/%s/tarball/%s";

        const auto url = fmt(urlFmt, host, getOwner(input), getRepo(input),
            input.getRev()->to_string(HashFormat::Base16, false));

        return DownloadUrl { url, headers };
    }

    void clone(const Input & input, const Path & destDir) const override
    {
        auto host = getHost(input);
        Input::fromURL(fmt("git+https://%s/%s/%s.git",
                host, getOwner(input), getRepo(input)))
            .applyOverrides(input.getRef(), input.getRev())
            .clone(destDir);
    }
};

struct GitLabInputScheme : GitArchiveInputScheme
{
    std::string_view schemeName() const override { return "gitlab"; }

    std::optional<std::pair<std::string, std::string>> accessHeaderFromToken(const std::string & token) const override
    {
        // Gitlab supports 4 kinds of authorization, two of which are
        // relevant here: OAuth2 and PAT (Private Access Token).  The
        // user can indicate which token is used by specifying the
        // token as <TYPE>:<VALUE>, where type is "OAuth2" or "PAT".
        // If the <TYPE> is unrecognized, this will fall back to
        // treating this simply has <HDRNAME>:<HDRVAL>.  See
        // https://docs.gitlab.com/12.10/ee/api/README.html#authentication
        auto fldsplit = token.find_first_of(':');
        // n.b. C++20 would allow: if (token.starts_with("OAuth2:")) ...
        if ("OAuth2" == token.substr(0, fldsplit))
            return std::make_pair("Authorization", fmt("Bearer %s", token.substr(fldsplit+1)));
        if ("PAT" == token.substr(0, fldsplit))
            return std::make_pair("Private-token", token.substr(fldsplit+1));
        warn("Unrecognized GitLab token type %s",  token.substr(0, fldsplit));
        return std::make_pair(token.substr(0,fldsplit), token.substr(fldsplit+1));
    }

    RefInfo getRevFromRef(nix::ref<Store> store, const Input & input) const override
    {
        auto host = maybeGetStrAttr(input.attrs, "host").value_or("gitlab.com");
        // See rate limiting note below
        auto url = fmt("https://%s/api/v4/projects/%s%%2F%s/repository/commits?ref_name=%s",
            host, getStrAttr(input.attrs, "owner"), getStrAttr(input.attrs, "repo"), *input.getRef());

        Headers headers = makeHeadersWithAuthTokens(host);

        auto json = nlohmann::json::parse(
            readFile(
                store->toRealPath(
                    downloadFile(store, url, "source", false, headers).storePath)));

        return RefInfo {
            .rev = Hash::parseAny(std::string(json[0]["id"]), HashAlgorithm::SHA1)
        };
    }

    DownloadUrl getDownloadUrl(const Input & input) const override
    {
        // This endpoint has a rate limit threshold that may be
        // server-specific and vary based whether the user is
        // authenticated via an accessToken or not, but the usual rate
        // is 10 reqs/sec/ip-addr.  See
        // https://docs.gitlab.com/ee/user/gitlab_com/index.html#gitlabcom-specific-rate-limits
        auto host = maybeGetStrAttr(input.attrs, "host").value_or("gitlab.com");
        auto url = fmt("https://%s/api/v4/projects/%s%%2F%s/repository/archive.tar.gz?sha=%s",
            host, getStrAttr(input.attrs, "owner"), getStrAttr(input.attrs, "repo"),
            input.getRev()->to_string(HashFormat::Base16, false));

        Headers headers = makeHeadersWithAuthTokens(host);
        return DownloadUrl { url, headers };
    }

    void clone(const Input & input, const Path & destDir) const override
    {
        auto host = maybeGetStrAttr(input.attrs, "host").value_or("gitlab.com");
        // FIXME: get username somewhere
        Input::fromURL(fmt("git+https://%s/%s/%s.git",
                host, getStrAttr(input.attrs, "owner"), getStrAttr(input.attrs, "repo")))
            .applyOverrides(input.getRef(), input.getRev())
            .clone(destDir);
    }
};

struct SourceHutInputScheme : GitArchiveInputScheme
{
    std::string_view schemeName() const override { return "sourcehut"; }

    std::optional<std::pair<std::string, std::string>> accessHeaderFromToken(const std::string & token) const override
    {
        // SourceHut supports both PAT and OAuth2. See
        // https://man.sr.ht/meta.sr.ht/oauth.md
        return std::pair<std::string, std::string>("Authorization", fmt("Bearer %s", token));
        // Note: This currently serves no purpose, as this kind of authorization
        // does not allow for downloading tarballs on sourcehut private repos.
        // Once it is implemented, however, should work as expected.
    }

    RefInfo getRevFromRef(nix::ref<Store> store, const Input & input) const override
    {
        // TODO: In the future, when the sourcehut graphql API is implemented for mercurial
        // and with anonymous access, this method should use it instead.

        auto ref = *input.getRef();

        auto host = maybeGetStrAttr(input.attrs, "host").value_or("git.sr.ht");
        auto base_url = fmt("https://%s/%s/%s",
            host, getStrAttr(input.attrs, "owner"), getStrAttr(input.attrs, "repo"));

        Headers headers = makeHeadersWithAuthTokens(host);

        std::string refUri;
        if (ref == "HEAD") {
            auto file = store->toRealPath(
                downloadFile(store, fmt("%s/HEAD", base_url), "source", false, headers).storePath);
            std::ifstream is(file);
            std::string line;
            getline(is, line);

            auto remoteLine = git::parseLsRemoteLine(line);
            if (!remoteLine) {
                throw BadURL("in '%d', couldn't resolve HEAD ref '%d'", input.to_string(), ref);
            }
            refUri = remoteLine->target;
        } else {
            refUri = fmt("refs/(heads|tags)/%s", ref);
        }
        std::regex refRegex(refUri);

        auto file = store->toRealPath(
            downloadFile(store, fmt("%s/info/refs", base_url), "source", false, headers).storePath);
        std::ifstream is(file);

        std::string line;
        std::optional<std::string> id;
        while(!id && getline(is, line)) {
            auto parsedLine = git::parseLsRemoteLine(line);
            if (parsedLine && parsedLine->reference && std::regex_match(*parsedLine->reference, refRegex))
                id = parsedLine->target;
        }

        if (!id)
            throw BadURL("in '%d', couldn't find ref '%d'", input.to_string(), ref);

        return RefInfo {
            .rev = Hash::parseAny(*id, HashAlgorithm::SHA1)
        };
    }

    DownloadUrl getDownloadUrl(const Input & input) const override
    {
        auto host = maybeGetStrAttr(input.attrs, "host").value_or("git.sr.ht");
        auto url = fmt("https://%s/%s/%s/archive/%s.tar.gz",
            host, getStrAttr(input.attrs, "owner"), getStrAttr(input.attrs, "repo"),
            input.getRev()->to_string(HashFormat::Base16, false));

        Headers headers = makeHeadersWithAuthTokens(host);
        return DownloadUrl { url, headers };
    }

    void clone(const Input & input, const Path & destDir) const override
    {
        auto host = maybeGetStrAttr(input.attrs, "host").value_or("git.sr.ht");
        Input::fromURL(fmt("git+https://%s/%s/%s",
                host, getStrAttr(input.attrs, "owner"), getStrAttr(input.attrs, "repo")))
            .applyOverrides(input.getRef(), input.getRev())
            .clone(destDir);
    }
};

static auto rGitHubInputScheme = OnStartup([] { registerInputScheme(std::make_unique<GitHubInputScheme>()); });
static auto rGitLabInputScheme = OnStartup([] { registerInputScheme(std::make_unique<GitLabInputScheme>()); });
static auto rSourceHutInputScheme = OnStartup([] { registerInputScheme(std::make_unique<SourceHutInputScheme>()); });

}
