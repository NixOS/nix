#include "nix/fetchers/fetchers.hh"
#include "nix/fetchers/git-utils.hh"
#include "nix/util/url-parts.hh"
#include "nix/store/path.hh"

namespace nix::fetchers {

std::regex flakeRegex("[a-zA-Z][a-zA-Z0-9_-]*", std::regex::ECMAScript);

struct IndirectInputScheme : InputScheme
{
    std::optional<Input> inputFromURL(const Settings & settings, const ParsedURL & url, bool requireTree) const override
    {
        if (url.scheme != "flake")
            return {};

        /* This ignores empty path segments for back-compat. Older versions used a tokenizeString here. */
        auto path = url.pathSegments(/*skipEmpty=*/true) | std::ranges::to<std::vector<std::string>>();

        std::optional<Hash> rev;
        std::optional<std::string> ref;

        if (path.size() == 1) {
        } else if (path.size() == 2) {
            if (std::regex_match(path[1], revRegex))
                rev = Hash::parseAny(path[1], HashAlgorithm::SHA1);
            else if (isLegalRefName(path[1]))
                ref = path[1];
            else
                throw BadURL("in flake URL '%s', '%s' is not a commit hash or branch/tag name", url, path[1]);
        } else if (path.size() == 3) {
            if (!isLegalRefName(path[1]))
                throw BadURL("in flake URL '%s', '%s' is not a branch/tag name", url, path[1]);
            ref = path[1];
            if (!std::regex_match(path[2], revRegex))
                throw BadURL("in flake URL '%s', '%s' is not a commit hash", url, path[2]);
            rev = Hash::parseAny(path[2], HashAlgorithm::SHA1);
        } else
            throw BadURL("GitHub URL '%s' is invalid", url);

        std::string id = path[0];
        if (!std::regex_match(id, flakeRegex))
            throw BadURL("'%s' is not a valid flake ID", id);

        // FIXME: forbid query params?

        Input input{settings};
        input.attrs.insert_or_assign("type", "indirect");
        input.attrs.insert_or_assign("id", id);
        if (rev)
            input.attrs.insert_or_assign("rev", rev->gitRev());
        if (ref)
            input.attrs.insert_or_assign("ref", *ref);

        return input;
    }

    std::string_view schemeName() const override
    {
        return "indirect";
    }

    StringSet allowedAttrs() const override
    {
        return {
            "id",
            "ref",
            "rev",
            "narHash",
        };
    }

    std::optional<Input> inputFromAttrs(const Settings & settings, const Attrs & attrs) const override
    {
        auto id = getStrAttr(attrs, "id");
        if (!std::regex_match(id, flakeRegex))
            throw BadURL("'%s' is not a valid flake ID", id);

        Input input{settings};
        input.attrs = attrs;
        return input;
    }

    ParsedURL toURL(const Input & input) const override
    {
        ParsedURL url{
            .scheme = "flake",
            .path = {getStrAttr(input.attrs, "id")},
        };
        if (auto ref = input.getRef()) {
            url.path.push_back(*ref);
        };
        if (auto rev = input.getRev()) {
            url.path.push_back(rev->gitRev());
        };
        return url;
    }

    Input applyOverrides(const Input & _input, std::optional<std::string> ref, std::optional<Hash> rev) const override
    {
        auto input(_input);
        if (rev)
            input.attrs.insert_or_assign("rev", rev->gitRev());
        if (ref)
            input.attrs.insert_or_assign("ref", *ref);
        return input;
    }

    std::pair<ref<SourceAccessor>, Input> getAccessor(ref<Store> store, const Input & input) const override
    {
        throw Error("indirect input '%s' cannot be fetched directly", input.to_string());
    }

    std::optional<ExperimentalFeature> experimentalFeature() const override
    {
        return Xp::Flakes;
    }

    bool isDirect(const Input & input) const override
    {
        return false;
    }
};

static auto rIndirectInputScheme = OnStartup([] { registerInputScheme(std::make_unique<IndirectInputScheme>()); });

} // namespace nix::fetchers
