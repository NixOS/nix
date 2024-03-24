#pragma once
///@file

#include "types.hh"
#include "error.hh"
#include "logging.hh"

#include <boost/lexical_cast.hpp>

#include <functional>
#include <map>
#include <sstream>
#include <optional>

namespace nix {

void initLibUtil();

/**
 * Convert a list of strings to a null-terminated vector of `char
 * *`s. The result must not be accessed beyond the lifetime of the
 * list of strings.
 */
std::vector<char *> stringsToCharPtrs(const Strings & ss);


MakeError(FormatError, Error);


/**
 * String tokenizer.
 */
template<class C> C tokenizeString(std::string_view s, std::string_view separators = " \t\n\r");


/**
 * Concatenate the given strings with a separator between the
 * elements.
 */
template<class C>
std::string concatStringsSep(const std::string_view sep, const C & ss)
{
    size_t size = 0;
    // need a cast to string_view since this is also called with Symbols
    for (const auto & s : ss) size += sep.size() + std::string_view(s).size();
    std::string s;
    s.reserve(size);
    for (auto & i : ss) {
        if (s.size() != 0) s += sep;
        s += i;
    }
    return s;
}

template<class ... Parts>
auto concatStrings(Parts && ... parts)
    -> std::enable_if_t<(... && std::is_convertible_v<Parts, std::string_view>), std::string>
{
    std::string_view views[sizeof...(parts)] = { parts... };
    return concatStringsSep({}, views);
}


/**
 * Add quotes around a collection of strings.
 */
template<class C> Strings quoteStrings(const C & c)
{
    Strings res;
    for (auto & s : c)
        res.push_back("'" + s + "'");
    return res;
}

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
std::string replaceStrings(
    std::string s,
    std::string_view from,
    std::string_view to);


std::string rewriteStrings(std::string s, const StringMap & rewrites);


/**
 * Parse a string into an integer.
 */
template<class N>
std::optional<N> string2Int(const std::string_view s)
{
    if (s.substr(0, 1) == "-" && !std::numeric_limits<N>::is_signed)
        return std::nullopt;
    try {
        return boost::lexical_cast<N>(s.data(), s.size());
    } catch (const boost::bad_lexical_cast &) {
        return std::nullopt;
    }
}

/**
 * Like string2Int(), but support an optional suffix 'K', 'M', 'G' or
 * 'T' denoting a binary unit prefix.
 */
template<class N>
N string2IntWithUnitPrefix(std::string_view s)
{
    N multiplier = 1;
    if (!s.empty()) {
        char u = std::toupper(*s.rbegin());
        if (std::isalpha(u)) {
            if (u == 'K') multiplier = 1ULL << 10;
            else if (u == 'M') multiplier = 1ULL << 20;
            else if (u == 'G') multiplier = 1ULL << 30;
            else if (u == 'T') multiplier = 1ULL << 40;
            else throw UsageError("invalid unit specifier '%1%'", u);
            s.remove_suffix(1);
        }
    }
    if (auto n = string2Int<N>(s))
        return *n * multiplier;
    throw UsageError("'%s' is not an integer", s);
}

/**
 * Parse a string into a float.
 */
template<class N>
std::optional<N> string2Float(const std::string_view s)
{
    try {
        return boost::lexical_cast<N>(s.data(), s.size());
    } catch (const boost::bad_lexical_cast &) {
        return std::nullopt;
    }
}


/**
 * Convert a little-endian integer to host order.
 */
template<typename T>
T readLittleEndian(unsigned char * p)
{
    T x = 0;
    for (size_t i = 0; i < sizeof(x); ++i, ++p) {
        x |= ((T) *p) << (i * 8);
    }
    return x;
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
 */
std::string shellEscape(const std::string_view s);


/* Exception handling in destructors: print an error message, then
   ignore the exception. */
void ignoreException(Verbosity lvl = lvlError);



/**
 * Tree formatting.
 */
constexpr char treeConn[] = "├───";
constexpr char treeLast[] = "└───";
constexpr char treeLine[] = "│   ";
constexpr char treeNull[] = "    ";


/**
 * Base64 encoding/decoding.
 */
std::string base64Encode(std::string_view s);
std::string base64Decode(std::string_view s);


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
 * Get a value for the specified key from an associate container.
 */
template <class T>
const typename T::mapped_type * get(const T & map, const typename T::key_type & key)
{
    auto i = map.find(key);
    if (i == map.end()) return nullptr;
    return &i->second;
}

template <class T>
typename T::mapped_type * get(T & map, const typename T::key_type & key)
{
    auto i = map.find(key);
    if (i == map.end()) return nullptr;
    return &i->second;
}

/**
 * Get a value for the specified key from an associate container, or a default value if the key isn't present.
 */
template <class T>
const typename T::mapped_type & getOr(T & map,
    const typename T::key_type & key,
    const typename T::mapped_type & defaultValue)
{
    auto i = map.find(key);
    if (i == map.end()) return defaultValue;
    return i->second;
}

/**
 * Remove and return the first item from a container.
 */
template <class T>
std::optional<typename T::value_type> remove_begin(T & c)
{
    auto i = c.begin();
    if (i == c.end()) return {};
    auto v = std::move(*i);
    c.erase(i);
    return v;
}


/**
 * Remove and return the first item from a container.
 */
template <class T>
std::optional<typename T::value_type> pop(T & c)
{
    if (c.empty()) return {};
    auto v = std::move(c.front());
    c.pop();
    return v;
}


template<typename T>
class Callback;


/**
 * A RAII helper that increments a counter on construction and
 * decrements it on destruction.
 */
template<typename T>
struct MaintainCount
{
    T & counter;
    long delta;
    MaintainCount(T & counter, long delta = 1) : counter(counter), delta(delta) { counter += delta; }
    ~MaintainCount() { counter -= delta; }
};


/**
 * A Rust/Python-like enumerate() iterator adapter.
 *
 * Borrowed from http://reedbeta.com/blog/python-like-enumerate-in-cpp17.
 */
template <typename T,
          typename TIter = decltype(std::begin(std::declval<T>())),
          typename = decltype(std::end(std::declval<T>()))>
constexpr auto enumerate(T && iterable)
{
    struct iterator
    {
        size_t i;
        TIter iter;
        constexpr bool operator != (const iterator & other) const { return iter != other.iter; }
        constexpr void operator ++ () { ++i; ++iter; }
        constexpr auto operator * () const { return std::tie(i, *iter); }
    };

    struct iterable_wrapper
    {
        T iterable;
        constexpr auto begin() { return iterator{ 0, std::begin(iterable) }; }
        constexpr auto end() { return iterator{ 0, std::end(iterable) }; }
    };

    return iterable_wrapper{ std::forward<T>(iterable) };
}


/**
 * C++17 std::visit boilerplate
 */
template<class... Ts> struct overloaded : Ts... { using Ts::operator()...; };
template<class... Ts> overloaded(Ts...) -> overloaded<Ts...>;


std::string showBytes(uint64_t bytes);


/**
 * Provide an addition operator between strings and string_views
 * inexplicably omitted from the standard library.
 */
inline std::string operator + (const std::string & s1, std::string_view s2)
{
    auto s = s1;
    s.append(s2);
    return s;
}

inline std::string operator + (std::string && s, std::string_view s2)
{
    s.append(s2);
    return std::move(s);
}

inline std::string operator + (std::string_view s1, const char * s2)
{
    std::string s;
    s.reserve(s1.size() + strlen(s2));
    s.append(s1);
    s.append(s2);
    return s;
}

}
