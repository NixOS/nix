#pragma once

#include "strings.hh"

namespace nix {

template<class C>
C tokenizeString(std::string_view s, std::string_view separators)
{
    C result;
    auto pos = s.find_first_not_of(separators, 0);
    while (pos != s.npos) {
        auto end = s.find_first_of(separators, pos + 1);
        if (end == s.npos)
            end = s.size();
        result.insert(result.end(), std::string(s, pos, end - pos));
        pos = s.find_first_not_of(separators, end);
    }
    return result;
}

template<class C>
std::string concatStringsSep(const std::string_view sep, const C & ss)
{
    size_t size = 0;
    bool tail = false;
    // need a cast to string_view since this is also called with Symbols
    for (const auto & s : ss) {
        if (tail)
            size += sep.size();
        size += std::string_view(s).size();
        tail = true;
    }
    std::string s;
    s.reserve(size);
    tail = false;
    for (auto & i : ss) {
        if (tail)
            s += sep;
        s += i;
        tail = true;
    }
    return s;
}

template<class C>
std::string dropEmptyInitThenConcatStringsSep(const std::string_view sep, const C & ss)
{
    size_t size = 0;

    // TODO? remove to make sure we don't rely on the empty item ignoring behavior,
    //       or just get rid of this function by understanding the remaining calls.
    // for (auto & i : ss) {
    //     // Make sure we don't rely on the empty item ignoring behavior
    //     assert(!i.empty());
    //     break;
    // }

    // need a cast to string_view since this is also called with Symbols
    for (const auto & s : ss)
        size += sep.size() + std::string_view(s).size();
    std::string s;
    s.reserve(size);
    for (auto & i : ss) {
        if (s.size() != 0)
            s += sep;
        s += i;
    }
    return s;
}

} // namespace nix
