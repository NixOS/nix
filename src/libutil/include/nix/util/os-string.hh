#pragma once
///@file

#include <list>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace nix {

/**
 * Named because it is similar to the Rust type, except it is in the
 * native encoding not WTF-8.
 *
 * Same as `std::filesystem::path::value_type`, but manually defined to
 * avoid including a much more complex header.
 */
using OsChar =
#if defined(_WIN32) && !defined(__CYGWIN__)
    wchar_t
#else
    char
#endif
    ;

/**
 * Named because it is similar to the Rust type, except it is in the
 * native encoding not WTF-8.
 *
 * Same as `std::filesystem::path::string_type`, but manually defined
 * for the same reason as `OsChar`.
 */
using OsString = std::basic_string<OsChar>;

/**
 * `std::string_view` counterpart for `OsString`.
 */
using OsStringView = std::basic_string_view<OsChar>;

/**
 * `nix::StringMap` counterpart for `OsString`
 */
using OsStringMap = std::map<OsString, OsString, std::less<>>;

/**
 * `nix::Strings` counterpart for `OsString`
 */
using OsStrings = std::list<OsString>;

std::string os_string_to_string(OsStringView s);
std::string os_string_to_string(OsString s);

OsString string_to_os_string(std::string_view s);
OsString string_to_os_string(std::string s);

#ifndef _WIN32

inline std::string os_string_to_string(OsStringView s)
{
    return std::string(s);
}

inline std::string os_string_to_string(OsString s)
{
    return s;
}

inline OsString string_to_os_string(std::string_view s)
{
    return std::string(s);
}

inline OsString string_to_os_string(std::string s)
{
    return s;
}

#endif

/**
 * Convert a list of `std::string` to `OsStrings`.
 * Takes ownership to enable moves on Unix.
 */
inline OsStrings toOsStrings(std::list<std::string> ss)
{
#ifndef _WIN32
    // On Unix, OsStrings is std::list<std::string>, so just move
    return ss;
#else
    OsStrings result;
    for (auto & s : ss)
        result.push_back(string_to_os_string(std::move(s)));
    return result;
#endif
}

/**
 * Create string literals with the native character width of paths
 */
#ifndef _WIN32
#  define OS_STR(s) s
#else
#  define OS_STR(s) L##s
#endif

} // namespace nix
