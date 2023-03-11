#include "git-utils.hh"

#include <regex>

std::optional<std::string> parseListReferenceHeadRef(std::string_view line)
{
    const static std::regex head_ref_regex("^ref: ([^\\s]+)\\t+HEAD$");
    std::match_results<std::string_view::const_iterator> match;
    if (std::regex_match(line.cbegin(), line.cend(), match, head_ref_regex)) {
        return match[1];
    } else {
        return std::nullopt;
    }
}

std::optional<std::string> parseListReferenceForRev(std::string_view rev, std::string_view line)
{
    const static std::regex rev_regex("^([^\\t]+)\\t+(.*)$");
    std::match_results<std::string_view::const_iterator> match;
    if (!std::regex_match(line.cbegin(), line.cend(), match, rev_regex)) {
        return std::nullopt;
    }
    if (rev != match[2].str()) {
        return std::nullopt;
    }
    return match[1];
}
