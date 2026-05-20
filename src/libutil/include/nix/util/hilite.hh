#pragma once
///@file

#include <boost/regex.hpp>
#include <vector>
#include <string>

namespace nix {

/**
 * Highlight all the given matches in the given string `s` by wrapping
 * them between `prefix` and `postfix`.
 *
 * If some matches overlap, then their union will be wrapped rather
 * than the individual matches.
 */
std::string hiliteMatches(
    std::string_view s, std::vector<boost::smatch> matches, std::string_view prefix, std::string_view postfix);

} // namespace nix
