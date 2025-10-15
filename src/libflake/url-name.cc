#include <regex>
#include <optional>
#include <string>

#include "nix/flake/url-name.hh"
#include "nix/util/strings.hh"
#include "nix/util/url.hh"

namespace nix {

static const std::string attributeNamePattern("[a-zA-Z0-9_-]+");
static const std::regex
    lastAttributeRegex("^((?:" + attributeNamePattern + "\\.)*)(" + attributeNamePattern + ")(\\^.*)?$");
static const std::string pathSegmentPattern("[a-zA-Z0-9_-]+");
static const std::regex lastPathSegmentRegex(".*/(" + pathSegmentPattern + ")");
static const std::regex secondPathSegmentRegex("(?:" + pathSegmentPattern + ")/(" + pathSegmentPattern + ")(?:/.*)?");
static const std::regex gitProviderRegex("github|gitlab|sourcehut");
static const std::regex gitSchemeRegex("git($|\\+.*)");

std::optional<std::string> getNameFromURL(const ParsedURL & url)
{
    std::smatch match;

    /* If there is a dir= argument, use its value */
    if (url.query.count("dir") > 0)
        return url.query.at("dir");

    /* If the fragment isn't a "default" and contains two attribute elements, use the last one */
    if (std::regex_match(url.fragment, match, lastAttributeRegex) && match.str(1) != "defaultPackage."
        && match.str(2) != "default") {
        return match.str(2);
    }

    /* This is not right, because special chars like slashes within the
       path fragments should be percent encoded, but I don't think any
       of the regexes above care. */
    auto path = concatStringsSep("/", url.path);

    /* If this is a github/gitlab/sourcehut flake, use the repo name */
    if (std::regex_match(url.scheme, gitProviderRegex) && std::regex_match(path, match, secondPathSegmentRegex))
        return match.str(1);

    /* If it is a regular git flake, use the directory name */
    if (std::regex_match(url.scheme, gitSchemeRegex) && std::regex_match(path, match, lastPathSegmentRegex))
        return match.str(1);

    /* If there is no fragment, take the last element of the path */
    if (std::regex_match(path, match, lastPathSegmentRegex))
        return match.str(1);

    /* If even that didn't work, the URL does not contain enough info to determine a useful name */
    return {};
}

} // namespace nix
