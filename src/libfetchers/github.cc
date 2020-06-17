#include "filetransfer.hh"
#include "cache.hh"
#include "fetchers.hh"
#include "globals.hh"
#include "store-api.hh"

#include <nlohmann/json.hpp>

namespace nix::fetchers {

std::regex ownerRegex("[a-zA-Z][a-zA-Z0-9_-]*", std::regex::ECMAScript);
std::regex repoRegex("[a-zA-Z][a-zA-Z0-9_-]*", std::regex::ECMAScript);

struct GitHubInput : Input
{
    std::string owner;
    std::string repo;
    std::optional<std::string> ref;
    std::optional<Hash> rev;

    std::string type() const override { return "github"; }

    bool operator ==(const Input & other) const override
    {
        auto other2 = dynamic_cast<const GitHubInput *>(&other);
        return
            other2
            && owner == other2->owner
            && repo == other2->repo
            && rev == other2->rev
            && ref == other2->ref;
    }

    bool isImmutable() const override
    {
        return (bool) rev || narHash;
    }

    std::optional<std::string> getRef() const override { return ref; }

    std::optional<Hash> getRev() const override { return rev; }

    ParsedURL toURL() const override
    {
        auto path = owner + "/" + repo;
        assert(!(ref && rev));
        if (ref) path += "/" + *ref;
        if (rev) path += "/" + rev->to_string(Base16, false);
        return ParsedURL {
            .scheme = "github",
            .path = path,
        };
    }

    Attrs toAttrsInternal() const override
    {
        Attrs attrs;
        attrs.emplace("owner", owner);
        attrs.emplace("repo", repo);
        if (ref)
            attrs.emplace("ref", *ref);
        if (rev)
            attrs.emplace("rev", rev->gitRev());
        return attrs;
    }

    std::pair<Tree, std::shared_ptr<const Input>> fetchTreeInternal(nix::ref<Store> store) const override
    {
        auto rev = this->rev;
        auto ref = this->ref.value_or("master");

        if (!rev) {
            auto url = fmt("https://api.github.com/repos/%s/%s/commits/%s",
                owner, repo, ref);
            auto json = nlohmann::json::parse(
                readFile(
                    store->toRealPath(
                        downloadFile(store, url, "source", false).storePath)));
            rev = Hash(std::string { json["sha"] }, htSHA1);
            debug("HEAD revision for '%s' is %s", url, rev->gitRev());
        }

        auto input = std::make_shared<GitHubInput>(*this);
        input->ref = {};
        input->rev = *rev;

        Attrs immutableAttrs({
            {"type", "git-tarball"},
            {"rev", rev->gitRev()},
        });

        if (auto res = getCache()->lookup(store, immutableAttrs)) {
            return {
                Tree{
                    .actualPath = store->toRealPath(res->second),
                    .storePath = std::move(res->second),
                    .info = TreeInfo {
                        .lastModified = getIntAttr(res->first, "lastModified"),
                    },
                },
                input
            };
        }

        // FIXME: use regular /archive URLs instead? api.github.com
        // might have stricter rate limits.

        auto url = fmt("https://api.github.com/repos/%s/%s/tarball/%s",
            owner, repo, rev->to_string(Base16, false));

        std::string accessToken = settings.githubAccessToken.get();
        if (accessToken != "")
            url += "?access_token=" + accessToken;

        auto tree = downloadTarball(store, url, "source", true);

        getCache()->add(
            store,
            immutableAttrs,
            {
                {"rev", rev->gitRev()},
                {"lastModified", *tree.info.lastModified}
            },
            tree.storePath,
            true);

        return {std::move(tree), input};
    }
};

struct GitHubInputScheme : InputScheme
{
    std::unique_ptr<Input> inputFromURL(const ParsedURL & url) override
    {
        if (url.scheme != "github") return nullptr;

        auto path = tokenizeString<std::vector<std::string>>(url.path, "/");
        auto input = std::make_unique<GitHubInput>();

        if (path.size() == 2) {
        } else if (path.size() == 3) {
            if (std::regex_match(path[2], revRegex))
                input->rev = Hash(path[2], htSHA1);
            else if (std::regex_match(path[2], refRegex))
                input->ref = path[2];
            else
                throw BadURL("in GitHub URL '%s', '%s' is not a commit hash or branch/tag name", url.url, path[2]);
        } else
            throw BadURL("GitHub URL '%s' is invalid", url.url);

        for (auto &[name, value] : url.query) {
            if (name == "rev") {
                if (input->rev)
                    throw BadURL("GitHub URL '%s' contains multiple commit hashes", url.url);
                input->rev = Hash(value, htSHA1);
            }
            else if (name == "ref") {
                if (!std::regex_match(value, refRegex))
                    throw BadURL("GitHub URL '%s' contains an invalid branch/tag name", url.url);
                if (input->ref)
                    throw BadURL("GitHub URL '%s' contains multiple branch/tag names", url.url);
                input->ref = value;
            }
        }

        if (input->ref && input->rev)
            throw BadURL("GitHub URL '%s' contains both a commit hash and a branch/tag name", url.url);

        input->owner = path[0];
        input->repo = path[1];

        return input;
    }

    std::unique_ptr<Input> inputFromAttrs(const Attrs & attrs) override
    {
        if (maybeGetStrAttr(attrs, "type") != "github") return {};

        for (auto & [name, value] : attrs)
            if (name != "type" && name != "owner" && name != "repo" && name != "ref" && name != "rev")
                throw Error("unsupported GitHub input attribute '%s'", name);

        auto input = std::make_unique<GitHubInput>();
        input->owner = getStrAttr(attrs, "owner");
        input->repo = getStrAttr(attrs, "repo");
        input->ref = maybeGetStrAttr(attrs, "ref");
        if (auto rev = maybeGetStrAttr(attrs, "rev"))
            input->rev = Hash(*rev, htSHA1);
        return input;
    }
};

static auto r1 = OnStartup([] { registerInputScheme(std::make_unique<GitHubInputScheme>()); });

}
