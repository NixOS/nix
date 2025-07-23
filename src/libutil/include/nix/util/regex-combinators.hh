#pragma once
///@file

#include <string_view>
#include <string>
#include <sstream>

namespace nix::regex {

// TODO use constexpr string building like
// https://github.com/akrzemi1/static_string/blob/master/include/ak_toolkit/static_string.hpp

static inline std::string either(std::string_view a, std::string_view b)
{
    std::stringstream ss;
    ss << a << "|" << b;
    return ss.str();
}

static inline std::string group(std::string_view a)
{
    std::stringstream ss;
    ss << "(" << a << ")";
    return ss.str();
}

static inline std::string list(std::string_view a)
{
    std::stringstream ss;
    ss << a << "(," << a << ")*";
    return ss.str();
}

} // namespace nix::regex
