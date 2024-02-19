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
typename std::string canonPathInner(
    std::string_view remaining,
    auto && hookComponent)
{
    assert(remaining != "");

    std::string result;
    result.reserve(256);

    while (true) {

        /* Skip slashes. */
        while (!remaining.empty() && remaining[0] == '/')
            remaining.remove_prefix(1);

        if (remaining.empty()) break;

        auto nextComp = ({
            auto nextPathSep = remaining.find('/');
            nextPathSep == remaining.npos ? remaining : remaining.substr(0, nextPathSep);
        });

        /* Ignore `.'. */
        if (nextComp == ".")
            remaining.remove_prefix(1);

        /* If `..', delete the last component. */
        else if (nextComp == "..")
        {
            if (!result.empty()) result.erase(result.rfind('/'));
            remaining.remove_prefix(2);
        }

        /* Normal component; copy it. */
        else {
            result += '/';
            if (const auto slash = remaining.find('/'); slash == result.npos) {
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
        result = "/";

    return result;
}

}
