#pragma once
/**
 * @file
 *
 * Pure (no IO) infrastructure just for defining other path types;
 * should not be used directly outside of utilities.
 */
#include <string>
#include <string_view>

namespace nix {

/**
 * Unix-style path primives.
 *
 * Nix'result own "logical" paths are always Unix-style. So this is always
 * used for that, and additionally used for native paths on Unix.
 */
struct UnixPathTrait
{
    using CharT = char;

    using String = std::string;

    using StringView = std::string_view;

    constexpr static char preferredSep = '/';

    static inline bool isPathSep(char c)
    {
        return c == '/';
    }

    static inline size_t findPathSep(StringView path, size_t from = 0)
    {
        return path.find('/', from);
    }

    static inline size_t rfindPathSep(StringView path, size_t from = StringView::npos)
    {
        return path.rfind('/', from);
    }
};


/**
 * Windows-style path primitives.
 *
 * The character type is a parameter because while windows paths rightly
 * work over UTF-16 (*) using `wchar_t`, at the current time we are
 * often manipulating them converted to UTF-8 (*) using `char`.
 *
 * (Actually neither are guaranteed to be valid unicode; both are
 * arbitrary non-0 8- or 16-bit bytes. But for charcters with specifical
 * meaning like '/', '\\', ':', etc., we refer to an encoding scheme,
 * and also for sake of UIs that display paths a text.)
 */
template<class CharT0>
struct WindowsPathTrait
{
    using CharT = CharT0;

    using String = std::basic_string<CharT>;

    using StringView = std::basic_string_view<CharT>;

    constexpr static CharT preferredSep = '\\';

    static inline bool isPathSep(CharT c)
    {
        return c == '/' || c == preferredSep;
    }

    static size_t findPathSep(StringView path, size_t from = 0)
    {
        size_t p1 = path.find('/', from);
        size_t p2 = path.find(preferredSep, from);
        return p1 == String::npos ? p2 :
               p2 == String::npos ? p1 :
               std::min(p1, p2);
    }

    static size_t rfindPathSep(StringView path, size_t from = String::npos)
    {
        size_t p1 = path.rfind('/', from);
        size_t p2 = path.rfind(preferredSep, from);
        return p1 == String::npos ? p2 :
               p2 == String::npos ? p1 :
               std::max(p1, p2);
    }
};


/**
 * @todo Revisit choice of `char` or `wchar_t` for `WindowsPathTrait`
 * argument.
 */
using NativePathTrait =
#ifdef _WIN32
    WindowsPathTrait<char>
#else
    UnixPathTrait
#endif
    ;


/**
 * Core pure path canonicalization algorithm.
 *
 * @param hookComponent
 *   A callback which is passed two arguments,
 *   references to
 *
 *   1. the result so far
 *
 *   2. the remaining path to resolve
 *
 *   This is a chance to modify those two paths in arbitrary way, e.g. if
 *   "result" points to a symlink.
 */
template<class PathDict>
typename PathDict::String canonPathInner(
    typename PathDict::StringView remaining,
    auto && hookComponent)
{
    assert(remaining != "");

    typename PathDict::String result;
    result.reserve(256);

    while (true) {

        /* Skip slashes. */
        while (!remaining.empty() && PathDict::isPathSep(remaining[0]))
            remaining.remove_prefix(1);

        if (remaining.empty()) break;

        auto nextComp = ({
            auto nextPathSep = PathDict::findPathSep(remaining);
            nextPathSep == remaining.npos ? remaining : remaining.substr(0, nextPathSep);
        });

        /* Ignore `.'. */
        if (nextComp == ".")
            remaining.remove_prefix(1);

        /* If `..', delete the last component. */
        else if (nextComp == "..")
        {
            if (!result.empty()) result.erase(PathDict::rfindPathSep(result));
            remaining.remove_prefix(2);
        }

        /* Normal component; copy it. */
        else {
            result += PathDict::preferredSep;
            if (const auto slash = PathDict::findPathSep(remaining); slash == result.npos) {
                result += remaining;
                remaining = {};
            } else {
                result += remaining.substr(0, slash);
                remaining = remaining.substr(slash);
            }

            hookComponent(result, remaining);
        }
    }

    if (result.empty())
        result = typename PathDict::String { PathDict::preferredSep };

    return result;
}

}
