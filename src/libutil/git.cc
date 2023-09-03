#include "git.hh"

#include <regex>

namespace nix {
namespace git {

std::optional<LsRemoteRefLine> parseLsRemoteLine(std::string_view line)
{
    const static std::regex line_regex("^(ref: *)?([^\\s]+)(?:\\t+(.*))?$");
    std::match_results<std::string_view::const_iterator> match;
    if (!std::regex_match(line.cbegin(), line.cend(), match, line_regex))
        return std::nullopt;

    return LsRemoteRefLine {
        .kind = match[1].length() == 0
            ? LsRemoteRefLine::Kind::Object
            : LsRemoteRefLine::Kind::Symbolic,
        .target = match[2],
        .reference = match[3].length() == 0 ? std::nullopt : std::optional<std::string>{ match[3] }
    };
}

}
}
