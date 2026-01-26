#pragma once

#include "nix/util/types.hh"
#include "nix/util/error.hh"

#include <cctype>
#include <filesystem>
#include <list>
#include <optional>
#include <set>
#include <string_view>
#include <string>
#include <type_traits>
#include <utility>
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

template<class... Parts>
auto concatStrings(Parts &&... parts)
    -> std::enable_if_t<(... && std::is_convertible_v<Parts, std::string_view>), std::string>
{
    std::string_view views[sizeof...(parts)] = {parts...};
    return concatStringsSep({}, views);
}

template<class... Parts>
auto concatStringsTo(std::string & out, Parts &&... parts)
    -> std::enable_if_t<(... && std::is_convertible_v<Parts, std::string_view>), void>
{
    static_assert(sizeof...(parts) > 0);
    std::string_view views[sizeof...(parts)] = {parts...};
    for (auto v : views)
        out.append(v);
}

/**
 * Add quotes around a string.
 */
inline std::string quoteString(std::string_view s, char quote = '\'')
{
    std::string result;
    result.reserve(s.size() + 2);
    result += quote;
    result += s;
    result += quote;
    return result;
}

/**
 * Add quotes around a collection of strings.
 */
template<class C>
Strings quoteStrings(const C & c, char quote = '\'')
{
    Strings res;
    for (auto & s : c)
        res.push_back(quoteString(s, quote));
    return res;
}

Strings quoteFSPaths(const std::set<std::filesystem::path> & paths, char quote = '\'');

/**
 * Remove trailing whitespace from a string.
 *
 * \todo return std::string_view.
 */
std::string chomp(std::string_view s);

/**
 * Remove whitespace from the start and end of a string.
 */
std::string trim(std::string_view s, std::string_view whitespace = " \n\r\t");

/**
 * Replace all occurrences of a string inside another string.
 */
std::string replaceStrings(std::string s, std::string_view from, std::string_view to);

std::string rewriteStrings(std::string s, const StringMap & rewrites);

/**
 * Parse a string into an integer.
 */
template<class N>
std::optional<N> string2Int(const std::string_view s);

/**
 * Parse a string into a float.
 */
template<class N>
std::optional<N> string2Float(const std::string_view s);

/**
 * Like string2Int(), but support an optional suffix 'K', 'M', 'G' or
 * 'T' denoting a binary unit prefix.
 */
template<class N>
N string2IntWithUnitPrefix(std::string_view s)
{
    uint64_t multiplier = 1;
    if (!s.empty()) {
        char u = std::toupper(*s.rbegin());
        if (std::isalpha(u)) {
            if (u == 'K')
                multiplier = 1ULL << 10;
            else if (u == 'M')
                multiplier = 1ULL << 20;
            else if (u == 'G')
                multiplier = 1ULL << 30;
            else if (u == 'T')
                multiplier = 1ULL << 40;
            else
                throw UsageError("invalid unit specifier '%1%'", u);
            s.remove_suffix(1);
        }
    }
    if (auto n = string2Int<N>(s))
        return *n * multiplier;
    throw UsageError("'%s' is not an integer", s);
}

/**
 * @return true iff `s` starts with `prefix`.
 */
bool hasPrefix(std::string_view s, std::string_view prefix);

/**
 * @return true iff `s` ends in `suffix`.
 */
bool hasSuffix(std::string_view s, std::string_view suffix);

/**
 * Convert a string to lower case.
 */
std::string toLower(std::string s);

/**
 * Escape a string as a shell word.
 *
 * This always adds single quotes, even if escaping is not strictly necessary.
 * So both
 * - `"hello world"` -> `"'hello world'"`, which needs escaping because of the space
 * - `"echo"` -> `"'echo'"`, which doesn't need escaping
 */
std::string escapeShellArgAlways(const std::string_view s);

/**
 * Remove common leading whitespace from the lines in the string
 * 's'. For example, if every line is indented by at least 3 spaces,
 * then we remove 3 spaces from the start of every line.
 */
std::string stripIndentation(std::string_view s);

/**
 * Get the prefix of 's' up to and excluding the next line break (LF
 * optionally preceded by CR), and the remainder following the line
 * break.
 */
std::pair<std::string_view, std::string_view> getLine(std::string_view s);

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

/**
 * Check that the string does not contain any NUL bytes and return c_str().
 * @throws Error if str contains '\0' bytes.
 */
const char * requireCString(const std::string & str);

} // namespace nix
