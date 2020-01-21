#include "fetchers.hh"
#include "parse.hh"
#include "regex.hh"

namespace nix::fetchers {

std::regex flakeRegex("[a-zA-Z][a-zA-Z0-9_-]*", std::regex::ECMAScript);

struct IndirectInput : Input
{
    std::string id;
    std::optional<Hash> rev;
    std::optional<std::string> ref;

    bool operator ==(const Input & other) const override
    {
        auto other2 = dynamic_cast<const IndirectInput *>(&other);
        return
            other2
            && id == other2->id
            && rev == other2->rev
            && ref == other2->ref;
    }

    bool isDirect() const override
    {
        return false;
    }

    std::optional<std::string> getRef() const override { return ref; }

    std::optional<Hash> getRev() const override { return rev; }

    bool contains(const Input & other) const override
    {
        auto other2 = dynamic_cast<const IndirectInput *>(&other);
        return
            other2
            && id == other2->id
            && (!ref || ref == other2->ref)
            && (!rev || rev == other2->rev);
    }

    std::string to_string() const override
    {
        ParsedURL url;
        url.scheme = "flake";
        url.path = id;
        if (ref) { url.path += '/'; url.path += *ref; };
        if (rev) { url.path += '/'; url.path += rev->gitRev(); };
        return url.to_string();
    }

    std::shared_ptr<const Input> applyOverrides(
        std::optional<std::string> ref,
        std::optional<Hash> rev) const override
    {
        if (!ref && !rev) return shared_from_this();

        auto res = std::make_shared<IndirectInput>(*this);

        if (ref) res->ref = ref;
        if (rev) res->rev = rev;

        return res;
    }

    std::pair<Tree, std::shared_ptr<const Input>> fetchTreeInternal(nix::ref<Store> store) const override
    {
        throw Error("indirect input '%s' cannot be fetched directly", to_string());
    }
};

struct IndirectInputScheme : InputScheme
{
    std::unique_ptr<Input> inputFromURL(const ParsedURL & url) override
    {
        if (url.scheme != "flake") return nullptr;

        auto path = tokenizeString<std::vector<std::string>>(url.path, "/");
        auto input = std::make_unique<IndirectInput>();
        input->type = "indirect";

        if (path.size() == 1) {
        } else if (path.size() == 2) {
            if (std::regex_match(path[1], revRegex))
                input->rev = Hash(path[1], htSHA1);
            else if (std::regex_match(path[1], refRegex))
                input->ref = path[1];
            else
                throw BadURL("in flake URL '%s', '%s' is not a commit hash or branch/tag name", url.url, path[1]);
        } else if (path.size() == 3) {
            if (!std::regex_match(path[1], refRegex))
                throw BadURL("in flake URL '%s', '%s' is not a branch/tag name", url.url, path[1]);
            input->ref = path[1];
            if (!std::regex_match(path[2], revRegex))
                throw BadURL("in flake URL '%s', '%s' is not a commit hash", url.url, path[2]);
            input->rev = Hash(path[2], htSHA1);
        } else
            throw BadURL("GitHub URL '%s' is invalid", url.url);

        // FIXME: forbid query params?

        input->id = path[0];
        if (!std::regex_match(input->id, flakeRegex))
            throw BadURL("'%s' is not a valid flake ID", input->id);

        return input;
    }
};

static auto r1 = OnStartup([] { registerInputScheme(std::make_unique<IndirectInputScheme>()); });

}
