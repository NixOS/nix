#pragma once

#include <list>
#include <set>
#include <string_view>
#include <string>
#include <vector>

namespace nix {

/*
 * workaround for unavailable view() method (C++20) of std::ostringstream under MacOS with clang-16
 */
std::string_view toView(const std::ostringstream & os);

/**
 * String tokenizer.
 *
 * See also `basicSplitString()`, which preserves empty strings between separators, as well as at the start and end.
 */
template<class C, class CharT = char>
C basicTokenizeString(std::basic_string_view<CharT> s, std::basic_string_view<CharT> separators);

/**
 * Like `basicTokenizeString` but specialized to the default `char`
 */
template<class C>
C tokenizeString(std::string_view s, std::string_view separators = " \t\n\r");

extern template std::list<std::string> tokenizeString(std::string_view s, std::string_view separators);
extern template std::set<std::string> tokenizeString(std::string_view s, std::string_view separators);
extern template std::vector<std::string> tokenizeString(std::string_view s, std::string_view separators);

/**
 * Split a string, preserving empty strings between separators, as well as at the start and end.
 *
 * Returns a non-empty collection of strings.
 */
template<class C, class CharT = char>
C basicSplitString(std::basic_string_view<CharT> s, std::basic_string_view<CharT> separators);
template<typename C>
C splitString(std::string_view s, std::string_view separators);

extern template std::list<std::string> splitString(std::string_view s, std::string_view separators);
extern template std::set<std::string> splitString(std::string_view s, std::string_view separators);
extern template std::vector<std::string> splitString(std::string_view s, std::string_view separators);

/**
 * Concatenate the given strings with a separator between the elements.
 */
template<class C>
std::string concatStringsSep(const std::string_view sep, const C & ss);

extern template std::string concatStringsSep(std::string_view, const std::list<std::string> &);
extern template std::string concatStringsSep(std::string_view, const std::set<std::string> &);
extern template std::string concatStringsSep(std::string_view, const std::vector<std::string> &);

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

}
