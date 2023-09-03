#pragma once

#include <string_view>

namespace nix::regex {

// TODO use constexpr string building like
// https://github.com/akrzemi1/static_string/blob/master/include/ak_toolkit/static_string.hpp

static inline std::string either(std::string_view a, std::string_view b)
{
    return std::string { a } + "|" + b;
}

static inline std::string group(std::string_view a)
{
    return std::string { "(" } + a + ")";
}

static inline std::string many(std::string_view a)
{
    return std::string { "(?:" } + a + ")*";
}

static inline std::string list(std::string_view a)
{
    return std::string { a } + many(group("," + a));
}

}
