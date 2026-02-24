#include <algorithm>
#include <codecvt>
#include <iostream>
#include <locale>

#include "nix/util/os-string.hh"

#ifdef _WIN32

namespace nix {

std::string os_string_to_string(OsStringView s)
{
    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
    return converter.to_bytes(s.data(), s.data() + s.size());
}

std::string os_string_to_string(OsString s)
{
    return os_string_to_string(OsStringView{s});
}

OsString string_to_os_string(std::string_view s)
{
    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
    return converter.from_bytes(s.data(), s.data() + s.size());
}

OsString string_to_os_string(std::string s)
{
    return string_to_os_string(std::string_view{s});
}

} // namespace nix

#endif
