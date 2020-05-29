#include "filetransfer.hh"
#include "cache.hh"
#include "fetchers.hh"
#include "globals.hh"
#include "store-api.hh"

#include <nlohmann/json.hpp>

namespace nix::fetchers {

struct GitArchiveInput : Input
{
    std::string owner;
    std::string repo;
    std::optional<std::string> ref;
    std::optional<Hash> rev;

    virtual std::shared_ptr<GitArchiveInput> _clone() const = 0;

    bool operator ==(const Input & other) const override
    {
        auto other2 = dynamic_cast<const GitArchiveInput *>(&other);
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
            .scheme = type(),
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

    virtual Hash getRevFromRef(nix::ref<Store> store, std::string_view ref) const = 0;

    virtual std::string getDownloadUrl() const = 0;

    std::pair<Tree, std::shared_ptr<const Input>> fetchTreeInternal(nix::ref<Store> store) const override
    {
        auto rev = this->rev;
        auto ref = this->ref.value_or("master");

        if (!rev) rev = getRevFromRef(store, ref);

        auto input = _clone();
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

        auto url = input->getDownloadUrl();


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

    std::shared_ptr<const Input> applyOverrides(
        std::optional<std::string> ref,
        std::optional<Hash> rev) const override
    {
        if (!ref && !rev) return shared_from_this();

        auto res = _clone();

        if (ref) res->ref = ref;
        if (rev) res->rev = rev;

        return res;
    }
};

struct GitArchiveInputScheme : InputScheme
{
    std::string type;

    GitArchiveInputScheme(std::string && type) : type(type)
    { }

    virtual std::unique_ptr<GitArchiveInput> create() = 0;

    std::unique_ptr<Input> inputFromURL(const ParsedURL & url) override
    {
        if (url.scheme != type) return nullptr;

        auto path = tokenizeString<std::vector<std::string>>(url.path, "/");
        auto input = create();

        if (path.size() == 2) {
        } else if (path.size() == 3) {
            if (std::regex_match(path[2], revRegex))
                input->rev = Hash(path[2], htSHA1);
            else if (std::regex_match(path[2], refRegex))
                input->ref = path[2];
            else
                throw BadURL("in URL '%s', '%s' is not a commit hash or branch/tag name", url.url, path[2]);
        } else
            throw BadURL("URL '%s' is invalid", url.url);

        for (auto &[name, value] : url.query) {
            if (name == "rev") {
                if (input->rev)
                    throw BadURL("URL '%s' contains multiple commit hashes", url.url);
                input->rev = Hash(value, htSHA1);
            }
            else if (name == "ref") {
                if (!std::regex_match(value, refRegex))
                    throw BadURL("URL '%s' contains an invalid branch/tag name", url.url);
                if (input->ref)
                    throw BadURL("URL '%s' contains multiple branch/tag names", url.url);
                input->ref = value;
            }
        }

        if (input->ref && input->rev)
            throw BadURL("URL '%s' contains both a commit hash and a branch/tag name", url.url);

        input->owner = path[0];
        input->repo = path[1];

        return input;
    }

    std::unique_ptr<Input> inputFromAttrs(const Attrs & attrs) override
    {
        if (maybeGetStrAttr(attrs, "type") != type) return {};

        for (auto & [name, value] : attrs)
            if (name != "type" && name != "owner" && name != "repo" && name != "ref" && name != "rev")
                throw Error("unsupported input attribute '%s'", name);

        auto input = create();
        input->owner = getStrAttr(attrs, "owner");
        input->repo = getStrAttr(attrs, "repo");
        input->ref = maybeGetStrAttr(attrs, "ref");
        if (auto rev = maybeGetStrAttr(attrs, "rev"))
            input->rev = Hash(*rev, htSHA1);
        return input;
    }
};

struct GitHubInput : GitArchiveInput
{
    std::string type() const override { return "github"; }

    std::shared_ptr<GitArchiveInput> _clone() const override
    { return std::make_shared<GitHubInput>(*this); }

    Hash getRevFromRef(nix::ref<Store> store, std::string_view ref) const override
    {
        auto url = fmt("https://api.github.com/repos/%s/%s/commits/%s",
            owner, repo, ref);
        auto json = nlohmann::json::parse(
            readFile(
                store->toRealPath(
                    downloadFile(store, url, "source", false).storePath)));
        auto rev = Hash(json["sha"], htSHA1);
        debug("HEAD revision for '%s' is %s", url, rev.gitRev());
        return rev;
    }

    std::string getDownloadUrl() const override
    {
        // FIXME: use regular /archive URLs instead? api.github.com
        // might have stricter rate limits.

        auto url = fmt("https://api.github.com/repos/%s/%s/tarball/%s",
            owner, repo, rev->to_string(Base16, false));

        std::string accessToken = settings.githubAccessToken.get();
        if (accessToken != "")
            url += "?access_token=" + accessToken;

        return url;
    }

    void clone(const Path & destDir) const override
    {
        std::shared_ptr<const Input> input = inputFromURL(fmt("git+ssh://git@github.com/%s/%s.git", owner, repo));
        input = input->applyOverrides(ref.value_or("master"), rev);
        input->clone(destDir);
    }
};

struct GitHubInputScheme : GitArchiveInputScheme
{
    GitHubInputScheme() : GitArchiveInputScheme("github") { }

    std::unique_ptr<GitArchiveInput> create() override
    {
        return std::make_unique<GitHubInput>();
    }
};

struct GitLabInput : GitArchiveInput
{
    std::string type() const override { return "gitlab"; }

    std::shared_ptr<GitArchiveInput> _clone() const override
    { return std::make_shared<GitLabInput>(*this); }

    Hash getRevFromRef(nix::ref<Store> store, std::string_view ref) const override
    {
        auto url = fmt("https://gitlab.com/api/v4/projects/%s%%2F%s/repository/branches/%s",
            owner, repo, ref);
        auto json = nlohmann::json::parse(
            readFile(
                store->toRealPath(
                    downloadFile(store, url, "source", false).storePath)));
        auto rev = Hash(json["commit"]["id"], htSHA1);
        debug("HEAD revision for '%s' is %s", url, rev.gitRev());
        return rev;
    }

    std::string getDownloadUrl() const override
    {
        // FIXME: This endpoint has a rate limit threshold of 5 requests per minute.

        auto url = fmt("https://gitlab.com/api/v4/projects/%s%%2F%s/repository/archive.tar.gz?sha=%s",
            owner, repo, rev->to_string(Base16, false));

        /* # FIXME: add privat token auth (`curl --header "PRIVATE-TOKEN: <your_access_token>"`)
        std::string accessToken = settings.githubAccessToken.get();
        if (accessToken != "")
            url += "?access_token=" + accessToken;*/

        return url;
    }

    void clone(const Path & destDir) const override
    {
        std::shared_ptr<const Input> input = inputFromURL(fmt("git+ssh://git@gitlab.com/%s/%s.git", owner, repo));
        input = input->applyOverrides(ref.value_or("master"), rev);
        input->clone(destDir);
    }
};

struct GitLabInputScheme : GitArchiveInputScheme
{
    GitLabInputScheme() : GitArchiveInputScheme("gitlab") { }

    std::unique_ptr<GitArchiveInput> create() override
    {
        return std::make_unique<GitLabInput>();
    }
};

static auto r1 = OnStartup([] { registerInputScheme(std::make_unique<GitHubInputScheme>()); });
static auto r2 = OnStartup([] { registerInputScheme(std::make_unique<GitLabInputScheme>()); });

}
