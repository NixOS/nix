#include "filetransfer.hh"
#include "cache.hh"
#include "fetchers.hh"
#include "globals.hh"
#include "store-api.hh"
#include "types.hh"
#include "url-parts.hh"

#include <optional>
#include <nlohmann/json.hpp>

namespace nix::fetchers {

struct DownloadUrl
{
    std::string url;
    Headers headers;
};

// A github or gitlab host
const static std::string hostRegexS = "[a-zA-Z0-9.]*"; // FIXME: check
std::regex hostRegex(hostRegexS, std::regex::ECMAScript);

struct GitArchiveInputScheme : InputScheme
{
    virtual std::string type() = 0;

    virtual std::optional<std::pair<std::string, std::string>> accessHeaderFromToken(const std::string & token) const = 0;

    std::optional<Input> inputFromURL(const ParsedURL & url) override
    {
        if (url.scheme != type()) return {};

        auto path = tokenizeString<std::vector<std::string>>(url.path, "/");

        std::optional<Hash> rev;
        std::optional<std::string> ref;
        std::optional<std::string> host_url;

        if (path.size() == 2) {
        } else if (path.size() == 3) {
            if (std::regex_match(path[2], revRegex))
                rev = Hash::parseAny(path[2], htSHA1);
            else if (std::regex_match(path[2], refRegex))
                ref = path[2];
            else
                throw BadURL("in URL '%s', '%s' is not a commit hash or branch/tag name", url.url, path[2]);
        } else
            throw BadURL("URL '%s' is invalid", url.url);

        for (auto &[name, value] : url.query) {
            if (name == "rev") {
                if (rev)
                    throw BadURL("URL '%s' contains multiple commit hashes", url.url);
                rev = Hash::parseAny(value, htSHA1);
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
        input.attrs.insert_or_assign("type", type());
        input.attrs.insert_or_assign("owner", path[0]);
        input.attrs.insert_or_assign("repo", path[1]);
        if (rev) input.attrs.insert_or_assign("rev", rev->gitRev());
        if (ref) input.attrs.insert_or_assign("ref", *ref);
        if (host_url) input.attrs.insert_or_assign("host", *host_url);

        return input;
    }

    std::optional<Input> inputFromAttrs(const Attrs & attrs) override
    {
        if (maybeGetStrAttr(attrs, "type") != type()) return {};

        for (auto & [name, value] : attrs)
            if (name != "type" && name != "owner" && name != "repo" && name != "ref" && name != "rev" && name != "narHash" && name != "lastModified" && name != "host")
                throw Error("unsupported input attribute '%s'", name);

        getStrAttr(attrs, "owner");
        getStrAttr(attrs, "repo");

        Input input;
        input.attrs = attrs;
        return input;
    }

    ParsedURL toURL(const Input & input) override
    {
        auto owner = getStrAttr(input.attrs, "owner");
        auto repo = getStrAttr(input.attrs, "repo");
        auto ref = input.getRef();
        auto rev = input.getRev();
        auto path = owner + "/" + repo;
        assert(!(ref && rev));
        if (ref) path += "/" + *ref;
        if (rev) path += "/" + rev->to_string(Base16, false);
        return ParsedURL {
            .scheme = type(),
            .path = path,
        };
    }

    bool hasAllInfo(const Input & input) override
    {
        return input.getRev() && maybeGetIntAttr(input.attrs, "lastModified");
    }

    Input applyOverrides(
        const Input & _input,
        std::optional<std::string> ref,
        std::optional<Hash> rev) override
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
        auto tokens = settings.accessTokens.get();
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

    virtual Hash getRevFromRef(nix::ref<Store> store, const Input & input) const = 0;

    virtual DownloadUrl getDownloadUrl(const Input & input) const = 0;

    std::pair<Tree, Input> fetch(ref<Store> store, const Input & _input) override
    {
        Input input(_input);

        if (!maybeGetStrAttr(input.attrs, "ref")) input.attrs.insert_or_assign("ref", "HEAD");

        auto rev = input.getRev();
        if (!rev) rev = getRevFromRef(store, input);

        input.attrs.erase("ref");
        input.attrs.insert_or_assign("rev", rev->gitRev());

        Attrs immutableAttrs({
            {"type", "git-tarball"},
            {"rev", rev->gitRev()},
        });

        if (auto res = getCache()->lookup(store, immutableAttrs)) {
            input.attrs.insert_or_assign("lastModified", getIntAttr(res->first, "lastModified"));
            return {
                Tree {
                    store->toRealPath(store->makeFixedOutputPathFromCA(res->second)),
                    std::move(res->second),
                },
                input
            };
        }

        auto url = getDownloadUrl(input);

        auto [tree, lastModified] = downloadTarball(store, url.url, "source", true, url.headers);

        input.attrs.insert_or_assign("lastModified", lastModified);

        getCache()->add(
            store,
            immutableAttrs,
            {
                {"rev", rev->gitRev()},
                {"lastModified", lastModified}
            },
            tree.storePath,
            true);

        return {std::move(tree), input};
    }
};

struct GitHubInputScheme : GitArchiveInputScheme
{
    std::string type() override { return "github"; }

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

    Hash getRevFromRef(nix::ref<Store> store, const Input & input) const override
    {
        auto host = maybeGetStrAttr(input.attrs, "host").value_or("github.com");
        auto url = fmt("https://api.%s/repos/%s/%s/commits/%s", // FIXME: check
            host, getStrAttr(input.attrs, "owner"), getStrAttr(input.attrs, "repo"), *input.getRef());

        Headers headers = makeHeadersWithAuthTokens(host);

        auto json = nlohmann::json::parse(
            readFile(store->toRealPath(store->makeFixedOutputPathFromCA(
                downloadFile(store, url, "source", false, headers).storePath))));
        auto rev = Hash::parseAny(std::string { json["sha"] }, htSHA1);
        debug("HEAD revision for '%s' is %s", url, rev.gitRev());
        return rev;
    }

    DownloadUrl getDownloadUrl(const Input & input) const override
    {
        // FIXME: use regular /archive URLs instead? api.github.com
        // might have stricter rate limits.
        auto host = maybeGetStrAttr(input.attrs, "host").value_or("github.com");
        auto url = fmt("https://api.%s/repos/%s/%s/tarball/%s", // FIXME: check if this is correct for self hosted instances
            host, getStrAttr(input.attrs, "owner"), getStrAttr(input.attrs, "repo"),
            input.getRev()->to_string(Base16, false));

        Headers headers = makeHeadersWithAuthTokens(host);
        return DownloadUrl { url, headers };
    }

    void clone(const Input & input, const Path & destDir) override
    {
        auto host = maybeGetStrAttr(input.attrs, "host").value_or("github.com");
        Input::fromURL(fmt("git+ssh://git@%s/%s/%s.git",
                host, getStrAttr(input.attrs, "owner"), getStrAttr(input.attrs, "repo")))
            .applyOverrides(input.getRef().value_or("HEAD"), input.getRev())
            .clone(destDir);
    }
};

struct GitLabInputScheme : GitArchiveInputScheme
{
    std::string type() override { return "gitlab"; }

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
        return std::nullopt;
    }

    Hash getRevFromRef(nix::ref<Store> store, const Input & input) const override
    {
        auto host = maybeGetStrAttr(input.attrs, "host").value_or("gitlab.com");
        // See rate limiting note below
        auto url = fmt("https://%s/api/v4/projects/%s%%2F%s/repository/commits?ref_name=%s",
            host, getStrAttr(input.attrs, "owner"), getStrAttr(input.attrs, "repo"), *input.getRef());

        Headers headers = makeHeadersWithAuthTokens(host);

        auto json = nlohmann::json::parse(readFile(
            store->toRealPath(store->makeFixedOutputPathFromCA(
                downloadFile(store, url, "source", false, headers).storePath))));
        auto rev = Hash::parseAny(std::string(json[0]["id"]), htSHA1);
        debug("HEAD revision for '%s' is %s", url, rev.gitRev());
        return rev;
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
            input.getRev()->to_string(Base16, false));

        Headers headers = makeHeadersWithAuthTokens(host);
        return DownloadUrl { url, headers };
    }

    void clone(const Input & input, const Path & destDir) override
    {
        auto host = maybeGetStrAttr(input.attrs, "host").value_or("gitlab.com");
        // FIXME: get username somewhere
        Input::fromURL(fmt("git+ssh://git@%s/%s/%s.git",
                host, getStrAttr(input.attrs, "owner"), getStrAttr(input.attrs, "repo")))
            .applyOverrides(input.getRef().value_or("HEAD"), input.getRev())
            .clone(destDir);
    }
};

static auto rGitHubInputScheme = OnStartup([] { registerInputScheme(std::make_unique<GitHubInputScheme>()); });
static auto rGitLabInputScheme = OnStartup([] { registerInputScheme(std::make_unique<GitLabInputScheme>()); });

}
