///@file
///@brief A platform-agnostic implementation of the POSIX PATH environment variable logic
#include "environment-posix-path.hh"
#include "util.hh"
#include "strings.hh"

namespace nix {

std::string findExecutable(
    const std::string & name,
    std::optional<std::string> pathValue,
    std::function<bool(const std::string &)> isExecutable)
{
    // "If the pathname being sought contains a <slash>, the search through the path prefixes shall not be performed."
    // https://pubs.opengroup.org/onlinepubs/9699919799/basedefs/V1_chap08.html#tag_08_03
    if (name.empty() || name.find('/') != std::string::npos) {
        return name;
    }

    // "If PATH is unset or is set to null, the path search is implementation-defined."
    // https://pubs.opengroup.org/onlinepubs/9699919799/basedefs/V1_chap08.html#tag_08_03
    auto path = pathValue.value_or("");

    for (auto & dir : splitString<Strings>(path, ":")) {
        auto combined = dir.empty() ? name : dir + "/" + name;
        if (isExecutable(combined)) {
            return combined;
        }
    }
    return name;
}

} // namespace nix
