#pragma once
#include <string_view>

namespace nix {
/**
 * Split a string, preserving empty strings between separators, as well as at the start and end.
 *
 * Returns a non-empty collection of strings.
 */
template<typename C>
C splitString(std::string_view s, std::string_view separators);

/**
 * Concatenate the given strings with a separator between the
 * elements.
 */
template<class C>
std::string concatStringsSep(const std::string_view sep, const C & ss);

}
