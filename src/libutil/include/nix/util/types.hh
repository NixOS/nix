#pragma once
///@file

#include <list>
#include <set>
#include <string>
#include <map>
#include <variant>
#include <vector>

namespace nix {

typedef std::list<std::string> Strings;

/**
 * Alias to ordered std::string -> std::string map container with transparent comparator.
 *
 * Used instead of std::map<std::string, std::string> to use C++14 N3657 [1]
 * heterogenous lookup consistently across the whole codebase.
 * Transparent comparators get rid of creation of unnecessary
 * temporary variables when looking up keys by `std::string_view`
 * or C-style `const char *` strings.
 *
 * [1]: https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2013/n3657.htm
 */
using StringMap = std::map<std::string, std::string, std::less<>>;
/**
 * Alias to an ordered map of std::string -> std::string. Uses transparent comparator.
 *
 * @see StringMap
 */
using StringPairs = StringMap;

/**
 * Alias to ordered set container with transparent comparator.
 *
 * Used instead of std::set<std::string> to use C++14 N3657 [1]
 * heterogenous lookup consistently across the whole codebase.
 * Transparent comparators get rid of creation of unnecessary
 * temporary variables when looking up keys by `std::string_view`
 * or C-style `const char *` strings.
 *
 * [1]: https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2013/n3657.htm
 */
using StringSet = std::set<std::string, std::less<>>;

typedef std::vector<std::pair<std::string, std::string>> Headers;

/**
 * Helper class to run code at startup.
 */
template<typename T>
struct OnStartup
{
    OnStartup(T && t)
    {
        t();
    }
};

/**
 * Wrap bools to prevent string literals (i.e. 'char *') from being
 * cast to a bool in Attr.
 */
template<typename T>
struct Explicit
{
    T t;

    bool operator==(const Explicit<T> & other) const = default;

    bool operator<(const Explicit<T> & other) const
    {
        return t < other.t;
    }
};

/**
 * This wants to be a little bit like rust's Cow type.
 * Some parts of the evaluator benefit greatly from being able to reuse
 * existing allocations for strings, but have to be able to also use
 * newly allocated storage for values.
 *
 * We do not define implicit conversions, even with ref qualifiers,
 * since those can easily become ambiguous to the reader and can degrade
 * into copying behaviour we want to avoid.
 */
class BackedStringView
{
private:
    std::variant<std::string, std::string_view> data;

    /**
     * Needed to introduce a temporary since operator-> must return
     * a pointer. Without this we'd need to store the view object
     * even when we already own a string.
     */
    class Ptr
    {
    private:
        std::string_view view;
    public:
        Ptr(std::string_view view)
            : view(view)
        {
        }

        const std::string_view * operator->() const
        {
            return &view;
        }
    };

public:
    BackedStringView(std::string && s)
        : data(std::move(s))
    {
    }

    BackedStringView(std::string_view sv)
        : data(sv)
    {
    }

    template<size_t N>
    BackedStringView(const char (&lit)[N])
        : data(std::string_view(lit))
    {
    }

    BackedStringView(const BackedStringView &) = delete;
    BackedStringView & operator=(const BackedStringView &) = delete;

    /**
     * We only want move operations defined since the sole purpose of
     * this type is to avoid copies.
     */
    BackedStringView(BackedStringView && other) = default;
    BackedStringView & operator=(BackedStringView && other) = default;

    bool isOwned() const
    {
        return std::holds_alternative<std::string>(data);
    }

    std::string toOwned() &&
    {
        return isOwned() ? std::move(std::get<std::string>(data)) : std::string(std::get<std::string_view>(data));
    }

    std::string_view operator*() const
    {
        return isOwned() ? std::get<std::string>(data) : std::get<std::string_view>(data);
    }

    Ptr operator->() const
    {
        return Ptr(**this);
    }
};

} // namespace nix
