#pragma once

#include <list>
#include <set>
#include <string_view>
#include <string>
#include <vector>

namespace nix {

/**
 * Concatenate the given strings with a separator between the elements.
 */
template<class C>
std::string concatStringsSep(const std::string_view sep, const C & ss);

extern template std::string concatStringsSep(std::string_view, const std::list<std::string> &);
extern template std::string concatStringsSep(std::string_view, const std::set<std::string> &);
extern template std::string concatStringsSep(std::string_view, const std::vector<std::string> &);

}
