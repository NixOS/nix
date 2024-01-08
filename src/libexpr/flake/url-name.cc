#include "url-name.hh"
#include <regex>
#include <iostream>

namespace nix {

static const std::string attributeNamePattern("[a-zA-Z0-9_-]+");
static const std::regex lastAttributeRegex("(?:" + attributeNamePattern + "\\.)*(?!default)(" + attributeNamePattern +")(\\^.*)?");
static const std::string pathSegmentPattern("[a-zA-Z0-9_-]+");
static const std::regex lastPathSegmentRegex(".*/(" + pathSegmentPattern +")");
static const std::regex secondPathSegmentRegex("(?:" + pathSegmentPattern + ")/(" + pathSegmentPattern +")(?:/.*)?");
static const std::regex gitProviderRegex("github|gitlab|sourcehut");
static const std::regex gitSchemeRegex("git($|\\+.*)");
static const std::regex defaultOutputRegex(".*\\.default($|\\^.*)");

std::optional<std::string> getNameFromURL(const ParsedURL & url)
{
    std::smatch match;

    /* If there is a dir= argument, use its value */
    if (url.query.count("dir") > 0)
        return url.query.at("dir");

    /* If the fragment isn't a "default" and contains two attribute elements, use the last one */
    if (std::regex_match(url.fragment, match, lastAttributeRegex))
        return match.str(1);

    /* If this is a github/gitlab/sourcehut flake, use the repo name */
    if (std::regex_match(url.scheme, gitProviderRegex) && std::regex_match(url.path, match, secondPathSegmentRegex))
        return match.str(1);

    /* If it is a regular git flake, use the directory name */
    if (std::regex_match(url.scheme, gitSchemeRegex) && std::regex_match(url.path, match, lastPathSegmentRegex))
        return match.str(1);

    /* If everything failed but there is a non-default fragment, use it in full */
    if (!url.fragment.empty() && !std::regex_match(url.fragment, defaultOutputRegex))
        return url.fragment;

    /* If there is no fragment, take the last element of the path */
    if (std::regex_match(url.path, match, lastPathSegmentRegex))
        return match.str(1);

    /* If even that didn't work, the URL does not contain enough info to determine a useful name */
    return {};
}

}
