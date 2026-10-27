#include <algorithm>
#include <iostream>

#include "nix/util/os-string.hh"
#include "nix/util/strings-inline.hh"
#include "nix/util/wtf8.hh"

namespace nix {

#ifdef _WIN32
/* Windows file names are arbitrary 16-bit code units, so `wchar_t` has to be
   exactly that wide for the conversions below to be lossless. Guarded because
   `wchar_t` is 32 bits on Linux, where this file is not built. */
static_assert(sizeof(wchar_t) == 2, "Windows paths are UTF-16; wchar_t must be 16 bits");
#endif

std::string os_string_to_string(OsStringView s)
{
    /* `wchar_t` and `char16_t` are distinct types with the same
       representation here, so this copy is a reinterpretation without the
       aliasing question a cast would raise. */
    std::u16string units(s.begin(), s.end());
    return wtf8FromUtf16(units);
}

std::string os_string_to_string(OsString s)
{
    return os_string_to_string(OsStringView{s});
}

OsString string_to_os_string(std::string_view s)
{
    auto units = utf16FromWtf8(s);
    return OsString(units.begin(), units.end());
}

OsString string_to_os_string(std::string s)
{
    return string_to_os_string(std::string_view{s});
}

#ifdef _WIN32

template<class C>
C tokenizeString(OsStringView s, OsStringView separators)
{
    return basicTokenizeString<C, OsChar>(s, separators);
}

template std::list<OsString> tokenizeString(OsStringView s, OsStringView separators);
template std::vector<OsString> tokenizeString(OsStringView s, OsStringView separators);

#endif

} // namespace nix
