#include "fetchers.hh"
#include "url-parts.hh"

namespace nix::fetchers {

std::regex flakeRegex("[a-zA-Z][a-zA-Z0-9_-]*", std::regex::ECMAScript);

struct IndirectInputScheme : InputScheme
{
    std::optional<Input> inputFromURL(const ParsedURL & url) override
    {
        if (url.scheme != "flake") return {};

        auto path = tokenizeString<std::vector<std::string>>(url.path, "/");

        std::optional<Hash> rev;
        std::optional<std::string> ref;

        if (path.size() == 1) {
        } else if (path.size() == 2) {
            if (std::regex_match(path[1], revRegex))
                rev = Hash::parseAny(path[1], htSHA1);
            else if (std::regex_match(path[1], refRegex))
                ref = path[1];
            else
                throw BadURL("in flake URL '%s', '%s' is not a commit hash or branch/tag name", url.url, path[1]);
        } else if (path.size() == 3) {
            if (!std::regex_match(path[1], refRegex))
                throw BadURL("in flake URL '%s', '%s' is not a branch/tag name", url.url, path[1]);
            ref = path[1];
            if (!std::regex_match(path[2], revRegex))
                throw BadURL("in flake URL '%s', '%s' is not a commit hash", url.url, path[2]);
            rev = Hash::parseAny(path[2], htSHA1);
        } else
            throw BadURL("GitHub URL '%s' is invalid", url.url);

        std::string id = path[0];
        if (!std::regex_match(id, flakeRegex))
            throw BadURL("'%s' is not a valid flake ID", id);

        // FIXME: forbid query params?

        Input input;
        input.direct = false;
        input.attrs.insert_or_assign("type", "indirect");
        input.attrs.insert_or_assign("id", id);
        if (rev) input.attrs.insert_or_assign("rev", rev->gitRev());
        if (ref) input.attrs.insert_or_assign("ref", *ref);

        return input;
    }

    std::optional<Input> inputFromAttrs(const Attrs & attrs) override
    {
        if (maybeGetStrAttr(attrs, "type") != "indirect") return {};

        for (auto & [name, value] : attrs)
            if (name != "type" && name != "id" && name != "ref" && name != "rev" && name != "narHash")
                throw Error("unsupported indirect input attribute '%s'", name);

        auto id = getStrAttr(attrs, "id");
        if (!std::regex_match(id, flakeRegex))
            throw BadURL("'%s' is not a valid flake ID", id);

        Input input;
        input.direct = false;
        input.attrs = attrs;
        return input;
    }

    ParsedURL toURL(const Input & input) override
    {
        ParsedURL url;
        url.scheme = "flake";
        url.path = getStrAttr(input.attrs, "id");
        if (auto ref = input.getRef()) { url.path += '/'; url.path += *ref; };
        if (auto rev = input.getRev()) { url.path += '/'; url.path += rev->gitRev(); };
        return url;
    }

    bool hasAllInfo(const Input & input) override
    {
        return false;
    }

    Input applyOverrides(
        const Input & _input,
        std::optional<std::string> ref,
        std::optional<Hash> rev) override
    {
        auto input(_input);
        if (rev) input.attrs.insert_or_assign("rev", rev->gitRev());
        if (ref) input.attrs.insert_or_assign("ref", *ref);
        return input;
    }

    std::pair<Tree, Input> fetch(ref<Store> store, const Input & input) override
    {
        throw Error("indirect input '%s' cannot be fetched directly", input.to_string());
    }
};

static auto r1 = OnStartup([] { registerInputScheme(std::make_unique<IndirectInputScheme>()); });

}
