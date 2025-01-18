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

/**
 * Apply a function to the `iterable`'s items and concat them with `separator`.
 */
template<class C, class F>
std::string concatMapStringsSep(std::string_view separator, const C & iterable, F fn)
{
    std::vector<std::string> strings;
    strings.reserve(iterable.size());
    for (const auto & elem : iterable) {
        strings.push_back(fn(elem));
    }
    return concatStringsSep(separator, strings);
}

/**
 * Ignore any empty strings at the start of the list, and then concatenate the
 * given strings with a separator between the elements.
 *
 * @deprecated This function exists for historical reasons. You probably just
 *             want to use `concatStringsSep`.
 */
template<class C>
[[deprecated(
    "Consider removing the empty string dropping behavior. If acceptable, use concatStringsSep instead.")]] std::string
dropEmptyInitThenConcatStringsSep(const std::string_view sep, const C & ss);

extern template std::string dropEmptyInitThenConcatStringsSep(std::string_view, const std::list<std::string> &);
extern template std::string dropEmptyInitThenConcatStringsSep(std::string_view, const std::set<std::string> &);
extern template std::string dropEmptyInitThenConcatStringsSep(std::string_view, const std::vector<std::string> &);

/**
 * Shell split string: split a string into shell arguments, respecting quotes and backslashes.
 *
 * Used for NIX_SSHOPTS handling, which previously used `tokenizeString` and was broken by
 * Arguments that need to be passed to ssh with spaces in them.
 */
std::list<std::string> shellSplitString(std::string_view s);
}
