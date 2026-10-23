#pragma once
///@file

#include <list>
#include <map>
#include <optional>
#include <string>
#include <string_view>

#include "nix/util/strings.hh"

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

/**
 * Convert between the native path encoding and an 8-bit one.
 *
 * On Unix both are the same arbitrary byte string and these are the
 * identity. On Windows the 8-bit form is WTF-8, not strict UTF-8, because a
 * Windows file name is an arbitrary sequence of 16-bit code units and may
 * contain an unpaired surrogate that UTF-8 cannot represent. WTF-8 encodes
 * those, so every name round-trips.
 *
 * Neither direction throws. They previously went through
 * `std::codecvt_utf8_utf16`, which raised `std::range_error` on an unpaired
 * surrogate -- so a file with such a name could not be named at all, and the
 * failure surfaced far from the conversion.
 *
 * @see nix/util/wtf8.hh
 */
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

#ifdef _WIN32

template<class C>
C tokenizeString(OsStringView s, OsStringView separators = OS_STR(" \t\n\r"));

extern template std::list<OsString> tokenizeString(OsStringView s, OsStringView separators);
extern template std::vector<OsString> tokenizeString(OsStringView s, OsStringView separators);

#endif

} // namespace nix
