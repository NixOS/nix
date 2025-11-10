#pragma once
///@file

#include "nix/util/types.hh"
#include "nix/util/error.hh"
#include "nix/util/logging.hh"
#include "nix/util/strings.hh"

#include <functional>
#include <map>
#include <sstream>
#include <optional>
#include <ranges>

namespace nix {

void initLibUtil();

/**
 * Convert a list of strings to a null-terminated vector of `char
 * *`s. The result must not be accessed beyond the lifetime of the
 * list of strings.
 */
std::vector<char *> stringsToCharPtrs(const Strings & ss);

MakeError(FormatError, Error);

template<class... Parts>
auto concatStrings(Parts &&... parts)
    -> std::enable_if_t<(... && std::is_convertible_v<Parts, std::string_view>), std::string>
{
    std::string_view views[sizeof...(parts)] = {parts...};
    return concatStringsSep({}, views);
}

/**
 * Add quotes around a collection of strings.
 */
template<class C>
Strings quoteStrings(const C & c)
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
std::string replaceStrings(std::string s, std::string_view from, std::string_view to);

std::string rewriteStrings(std::string s, const StringMap & rewrites);

/**
 * Parse a string into an integer.
 */
template<class N>
std::optional<N> string2Int(const std::string_view s);

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

// Base also uses 'K', because it should also displayed as KiB => 100 Bytes => 0.1 KiB
#define NIX_UTIL_SIZE_UNITS               \
    NIX_UTIL_DEFINE_SIZE_UNIT(Base, 'K')  \
    NIX_UTIL_DEFINE_SIZE_UNIT(Kilo, 'K')  \
    NIX_UTIL_DEFINE_SIZE_UNIT(Mega, 'M')  \
    NIX_UTIL_DEFINE_SIZE_UNIT(Giga, 'G')  \
    NIX_UTIL_DEFINE_SIZE_UNIT(Tera, 'T')  \
    NIX_UTIL_DEFINE_SIZE_UNIT(Peta, 'P')  \
    NIX_UTIL_DEFINE_SIZE_UNIT(Exa, 'E')   \
    NIX_UTIL_DEFINE_SIZE_UNIT(Zetta, 'Z') \
    NIX_UTIL_DEFINE_SIZE_UNIT(Yotta, 'Y')

enum class SizeUnit {
#define NIX_UTIL_DEFINE_SIZE_UNIT(name, suffix) name,
    NIX_UTIL_SIZE_UNITS
#undef NIX_UTIL_DEFINE_SIZE_UNIT
};

constexpr inline auto sizeUnits = std::to_array<SizeUnit>({
#define NIX_UTIL_DEFINE_SIZE_UNIT(name, suffix) SizeUnit::name,
    NIX_UTIL_SIZE_UNITS
#undef NIX_UTIL_DEFINE_SIZE_UNIT
});

SizeUnit getSizeUnit(int64_t value);

/**
 * Returns the unit if all values would be rendered using the same unit
 * otherwise returns `std::nullopt`.
 */
std::optional<SizeUnit> getCommonSizeUnit(std::initializer_list<int64_t> values);

std::string renderSizeWithoutUnit(int64_t value, SizeUnit unit, bool align = false);

char getSizeUnitSuffix(SizeUnit unit);

/**
 * Pretty-print a byte value, e.g. 12433615056 is rendered as `11.6
 * GiB`. If `align` is set, the number will be right-justified by
 * padding with spaces on the left.
 */
std::string renderSize(int64_t value, bool align = false);

/**
 * Parse a string into a float.
 */
template<class N>
std::optional<N> string2Float(const std::string_view s);

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
 *
 * This always adds single quotes, even if escaping is not strictly necessary.
 * So both
 * - `"hello world"` -> `"'hello world'"`, which needs escaping because of the space
 * - `"echo"` -> `"'echo'"`, which doesn't need escaping
 */
std::string escapeShellArgAlways(const std::string_view s);

/**
 * Exception handling in destructors: print an error message, then
 * ignore the exception.
 *
 * If you're not in a destructor, you usually want to use `ignoreExceptionExceptInterrupt()`.
 *
 * This function might also be used in callbacks whose caller may not handle exceptions,
 * but ideally we propagate the exception using an exception_ptr in such cases.
 * See e.g. `PackBuilderContext`
 */
void ignoreExceptionInDestructor(Verbosity lvl = lvlError);

/**
 * Not destructor-safe.
 * Print an error message, then ignore the exception.
 * If the exception is an `Interrupted` exception, rethrow it.
 *
 * This may be used in a few places where Interrupt can't happen, but that's ok.
 */
void ignoreExceptionExceptInterrupt(Verbosity lvl = lvlError);

/**
 * Tree formatting.
 */
constexpr char treeConn[] = "├───";
constexpr char treeLast[] = "└───";
constexpr char treeLine[] = "│   ";
constexpr char treeNull[] = "    ";

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
template<class T, typename K>
const typename T::mapped_type * get(const T & map, const K & key)
{
    auto i = map.find(key);
    if (i == map.end())
        return nullptr;
    return &i->second;
}

template<class T, typename K>
typename T::mapped_type * get(T & map, const K & key)
{
    auto i = map.find(key);
    if (i == map.end())
        return nullptr;
    return &i->second;
}

/**
 * Deleted because this is use-after-free liability. Just don't pass temporaries to this overload set.
 */
template<class T, typename K>
typename T::mapped_type * get(T && map, const K & key) = delete;

/**
 * Look up a value in a `boost::concurrent_flat_map`.
 */
template<class T>
std::optional<typename T::mapped_type> getConcurrent(const T & map, const typename T::key_type & key)
{
    std::optional<typename T::mapped_type> res;
    map.cvisit(key, [&](auto & x) { res = x.second; });
    return res;
}

/**
 * Get a value for the specified key from an associate container, or a default value if the key isn't present.
 */
template<class T, typename K>
const typename T::mapped_type & getOr(T & map, const K & key, const typename T::mapped_type & defaultValue)
{
    auto i = map.find(key);
    if (i == map.end())
        return defaultValue;
    return i->second;
}

/**
 * Deleted because this is use-after-free liability. Just don't pass temporaries to this overload set.
 */
template<class T, typename K>
const typename T::mapped_type & getOr(T && map, const K & key, const typename T::mapped_type & defaultValue) = delete;

/**
 * Remove and return the first item from a container.
 */
template<class T>
std::optional<typename T::value_type> remove_begin(T & c)
{
    auto i = c.begin();
    if (i == c.end())
        return {};
    auto v = std::move(*i);
    c.erase(i);
    return v;
}

/**
 * Remove and return the first item from a container.
 */
template<class T>
std::optional<typename T::value_type> pop(T & c)
{
    if (c.empty())
        return {};
    auto v = std::move(c.front());
    c.pop();
    return v;
}

/**
 * Append items to a container. TODO: remove this once we can use
 * C++23's `append_range()`.
 */
template<class C, typename T>
void append(C & c, std::initializer_list<T> l)
{
    c.insert(c.end(), l.begin(), l.end());
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

    MaintainCount(T & counter, long delta = 1)
        : counter(counter)
        , delta(delta)
    {
        counter += delta;
    }

    ~MaintainCount()
    {
        counter -= delta;
    }
};

/**
 * A Rust/Python-like enumerate() iterator adapter.
 */
template<std::ranges::viewable_range R>
constexpr auto enumerate(R && range)
{
    /* Not std::views::enumerate because it uses difference_type for the index. */
    return std::views::zip(std::views::iota(size_t{0}), std::forward<R>(range));
}

/**
 * C++17 std::visit boilerplate
 */
template<class... Ts>
struct overloaded : Ts...
{
    using Ts::operator()...;
};
template<class... Ts>
overloaded(Ts...) -> overloaded<Ts...>;

/**
 * Provide an addition operator between strings and string_views
 * inexplicably omitted from the standard library.
 */
inline std::string operator+(const std::string & s1, std::string_view s2)
{
    std::string s;
    s.reserve(s1.size() + s2.size());
    s.append(s1);
    s.append(s2);
    return s;
}

inline std::string operator+(std::string && s, std::string_view s2)
{
    s.append(s2);
    return std::move(s);
}

inline std::string operator+(std::string_view s1, const char * s2)
{
    auto s2Size = strlen(s2);
    std::string s;
    s.reserve(s1.size() + s2Size);
    s.append(s1);
    s.append(s2, s2Size);
    return s;
}

} // namespace nix
