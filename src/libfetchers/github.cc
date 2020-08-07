#include "filetransfer.hh"
#include "cache.hh"
#include "fetchers.hh"
#include "globals.hh"
#include "store-api.hh"

#include <nlohmann/json.hpp>

namespace nix::fetchers {

// A github or gitlab url
const static std::string urlRegexS = "[a-zA-Z0-9.]*"; // FIXME: check
std::regex urlRegex(urlRegexS, std::regex::ECMAScript);

struct GitArchiveInputScheme : InputScheme
{
    virtual std::string type() = 0;

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
            else if (name == "url") {
                if (!std::regex_match(value, urlRegex))
                    throw BadURL("URL '%s' contains an invalid instance url", url.url);
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
        if (host_url) input.attrs.insert_or_assign("url", *host_url);

        return input;
    }

    std::optional<Input> inputFromAttrs(const Attrs & attrs) override
    {
        if (maybeGetStrAttr(attrs, "type") != type()) return {};

        for (auto & [name, value] : attrs)
            if (name != "type" && name != "owner" && name != "repo" && name != "ref" && name != "rev" && name != "narHash" && name != "lastModified")
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
        return input.getNarHash() && input.getRev() && maybeGetIntAttr(input.attrs, "lastModified");
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

    virtual Hash getRevFromRef(nix::ref<Store> store, const Input & input) const = 0;

    virtual std::string getDownloadUrl(const Input & input) const = 0;

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

        auto [tree, lastModified] = downloadTarball(store, url, "source", true);

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

    Hash getRevFromRef(nix::ref<Store> store, const Input & input) const override
    {
        auto host_url = maybeGetStrAttr(input.attrs, "url").value_or("github.com");
        auto url = fmt("https://api.%s/repos/%s/%s/commits/%s", // FIXME: check
            host_url, getStrAttr(input.attrs, "owner"), getStrAttr(input.attrs, "repo"), *input.getRef());
        auto json = nlohmann::json::parse(
            readFile(store->toRealPath(store->makeFixedOutputPathFromCA(
                downloadFile(store, url, "source", false).storePath))));
        auto rev = Hash::parseAny(std::string { json["sha"] }, htSHA1);
        debug("HEAD revision for '%s' is %s", url, rev.gitRev());
        return rev;
    }

    std::string getDownloadUrl(const Input & input) const override
    {
        // FIXME: use regular /archive URLs instead? api.github.com
        // might have stricter rate limits.
        auto host_url = maybeGetStrAttr(input.attrs, "url").value_or("github.com");
        auto url = fmt("https://api.%s/repos/%s/%s/tarball/%s", // FIXME: check if this is correct for self hosted instances
            host_url, getStrAttr(input.attrs, "owner"), getStrAttr(input.attrs, "repo"),
            input.getRev()->to_string(Base16, false));

        std::string accessToken = settings.githubAccessToken.get();
        if (accessToken != "")
            url += "?access_token=" + accessToken;

        return url;
    }

    void clone(const Input & input, const Path & destDir) override
    {
        auto host_url = maybeGetStrAttr(input.attrs, "url").value_or("github.com");
        Input::fromURL(fmt("git+ssh://git@%s/%s/%s.git",
                host_url, getStrAttr(input.attrs, "owner"), getStrAttr(input.attrs, "repo")))
            .applyOverrides(input.getRef().value_or("HEAD"), input.getRev())
            .clone(destDir);
    }
};

struct GitLabInputScheme : GitArchiveInputScheme
{
    std::string type() override { return "gitlab"; }

    Hash getRevFromRef(nix::ref<Store> store, const Input & input) const override
    {
        auto host_url = maybeGetStrAttr(input.attrs, "url").value_or("gitlab.com");
        auto url = fmt("https://%s/api/v4/projects/%s%%2F%s/repository/commits?ref_name=%s",
            host_url, getStrAttr(input.attrs, "owner"), getStrAttr(input.attrs, "repo"), *input.getRef());
        auto json = nlohmann::json::parse(readFile(
            store->toRealPath(store->makeFixedOutputPathFromCA(
                downloadFile(store, url, "source", false).storePath))));
        auto rev = Hash::parseAny(std::string(json[0]["id"]), htSHA1);
        debug("HEAD revision for '%s' is %s", url, rev.gitRev());
        return rev;
    }

    std::string getDownloadUrl(const Input & input) const override
    {
        // FIXME: This endpoint has a rate limit threshold of 5 requests per minute
        auto host_url = maybeGetStrAttr(input.attrs, "url").value_or("gitlab.com");
        auto url = fmt("https://%s/api/v4/projects/%s%%2F%s/repository/archive.tar.gz?sha=%s",
            host_url, getStrAttr(input.attrs, "owner"), getStrAttr(input.attrs, "repo"),
            input.getRev()->to_string(Base16, false));

        /* # FIXME: add privat token auth (`curl --header "PRIVATE-TOKEN: <your_access_token>"`)
        std::string accessToken = settings.githubAccessToken.get();
        if (accessToken != "")
            url += "?access_token=" + accessToken;*/

        return url;
    }

    void clone(const Input & input, const Path & destDir) override
    {
        auto host_url = maybeGetStrAttr(input.attrs, "url").value_or("gitlab.com");
        // FIXME: get username somewhere
        Input::fromURL(fmt("git+ssh://git@%s/%s/%s.git",
                host_url, getStrAttr(input.attrs, "owner"), getStrAttr(input.attrs, "repo")))
            .applyOverrides(input.getRef().value_or("HEAD"), input.getRev())
            .clone(destDir);
    }
};

static auto r1 = OnStartup([] { registerInputScheme(std::make_unique<GitHubInputScheme>()); });
static auto r2 = OnStartup([] { registerInputScheme(std::make_unique<GitLabInputScheme>()); });

}
