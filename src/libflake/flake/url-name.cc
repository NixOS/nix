#include "url-name.hh"
#include <regex>
#include <iostream>

namespace nix {

static const std::string attributeNamePattern("[a-zA-Z0-9_-]+");
static const std::regex lastAttributeRegex("^((?:" + attributeNamePattern + "\\.)*)(" + attributeNamePattern +")(\\^.*)?$");
static const std::string pathSegmentPattern("[a-zA-Z0-9_-]+");
static const std::regex lastPathSegmentRegex(".*/(" + pathSegmentPattern +")");
static const std::regex secondPathSegmentRegex("(?:" + pathSegmentPattern + ")/(" + pathSegmentPattern +")(?:/.*)?");
static const std::regex gitProviderRegex("github|gitlab|sourcehut");
static const std::regex gitSchemeRegex("git($|\\+.*)");

std::optional<std::string> getNameFromURL(const ParsedURL & url)
{
    std::smatch match;

    /* If there is a dir= argument, use its value */
    if (url.query.count("dir") > 0)
        return url.query.at("dir");

    /* If the fragment isn't a "default" and contains two attribute elements, use the last one */
    if (std::regex_match(url.fragment, match, lastAttributeRegex)
        && match.str(1) != "defaultPackage."
        && match.str(2) != "default") {
        return match.str(2);
    }

    /* If this is a github/gitlab/sourcehut flake, use the repo name */
    if (std::regex_match(url.scheme, gitProviderRegex) && std::regex_match(url.path, match, secondPathSegmentRegex))
        return match.str(1);

    /* If it is a regular git flake, use the directory name */
    if (std::regex_match(url.scheme, gitSchemeRegex) && std::regex_match(url.path, match, lastPathSegmentRegex))
        return match.str(1);

    /* If there is no fragment, take the last element of the path */
    if (std::regex_match(url.path, match, lastPathSegmentRegex))
        return match.str(1);

    /* If even that didn't work, the URL does not contain enough info to determine a useful name */
    return {};
}

}
