#pragma once

#include "strings.hh"

namespace nix {

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

} // namespace nix
