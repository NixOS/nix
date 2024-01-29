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
