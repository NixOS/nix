#pragma once

#include "nix/util/types.hh"

#include <list>
#include <optional>
#include <set>
#include <string_view>
#include <string>
#include <vector>

#include <boost/container/small_vector.hpp>

namespace nix {

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
extern template StringSet tokenizeString(std::string_view s, std::string_view separators);
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
extern template StringSet splitString(std::string_view s, std::string_view separators);
extern template std::vector<std::string> splitString(std::string_view s, std::string_view separators);

/**
 * Concatenate the given strings with a separator between the elements.
 */
template<class C>
std::string concatStringsSep(const std::string_view sep, const C & ss);

extern template std::string concatStringsSep(std::string_view, const std::list<std::string> &);
extern template std::string concatStringsSep(std::string_view, const StringSet &);
extern template std::string concatStringsSep(std::string_view, const std::vector<std::string> &);
extern template std::string concatStringsSep(std::string_view, const boost::container::small_vector<std::string, 64> &);

/**
 * Apply a function to the `iterable`'s items and concat them with `separator`.
 */
template<class C, class F>
std::string concatMapStringsSep(std::string_view separator, const C & iterable, F fn)
{
    boost::container::small_vector<std::string, 64> strings;
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
extern template std::string dropEmptyInitThenConcatStringsSep(std::string_view, const StringSet &);
extern template std::string dropEmptyInitThenConcatStringsSep(std::string_view, const std::vector<std::string> &);

/**
 * Shell split string: split a string into shell arguments, respecting quotes and backslashes.
 *
 * Used for NIX_SSHOPTS handling, which previously used `tokenizeString` and was broken by
 * Arguments that need to be passed to ssh with spaces in them.
 */
std::list<std::string> shellSplitString(std::string_view s);

/**
 * Conditionally wrap a string with prefix and suffix brackets.
 *
 * If `content` is empty, returns an empty string.
 * Otherwise, returns `prefix + content + suffix`.
 *
 * Example:
 *   optionalBracket(" (", "foo", ")") == " (foo)"
 *   optionalBracket(" (", "", ")") == ""
 *
 * Design note: this would have been called `optionalParentheses`, except this
 * function is more general and more explicit. Parentheses typically *also* need
 * to be prefixed with a space in order to fit nicely in a piece of natural
 * language.
 */
std::string optionalBracket(std::string_view prefix, std::string_view content, std::string_view suffix);

/**
 * Overload for optional content.
 *
 * If `content` is nullopt or contains an empty string, returns an empty string.
 * Otherwise, returns `prefix + *content + suffix`.
 *
 * Example:
 *   optionalBracket(" (", std::optional<std::string>("foo"), ")") == " (foo)"
 *   optionalBracket(" (", std::nullopt, ")") == ""
 *   optionalBracket(" (", std::optional<std::string>(""), ")") == ""
 */
template<typename T>
    requires std::convertible_to<T, std::string_view>
std::string optionalBracket(std::string_view prefix, const std::optional<T> & content, std::string_view suffix)
{
    if (!content || std::string_view(*content).empty()) {
        return "";
    }
    return optionalBracket(prefix, std::string_view(*content), suffix);
}

/**
 * Hash implementation that can be used for zero-copy heterogenous lookup from
 * P1690R1[1] in unordered containers.
 *
 * [1]: https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2019/p1690r1.html
 */
struct StringViewHash
{
private:
    using HashType = std::hash<std::string_view>;

public:
    using is_transparent = void;

    auto operator()(const char * str) const
    {
        /* This has a slight overhead due to an implicit strlen, but there isn't
           a good way around it because the hash value of all overloads must be
           consistent. Delegating to string_view is the solution initially proposed
           in P0919R3. */
        return HashType{}(std::string_view{str});
    }

    auto operator()(std::string_view str) const
    {
        return HashType{}(str);
    }

    auto operator()(const std::string & str) const
    {
        return HashType{}(std::string_view{str});
    }
};

} // namespace nix
