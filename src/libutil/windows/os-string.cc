#include <algorithm>
#include <codecvt>
#include <iostream>
#include <locale>

#include "nix/util/file-path.hh"
#include "nix/util/file-path-impl.hh"
#include "nix/util/util.hh"

#ifdef _WIN32

namespace nix {

std::string os_string_to_string(PathView::string_view path)
{
    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
    return converter.to_bytes(std::filesystem::path::string_type{path});
}

std::filesystem::path::string_type string_to_os_string(std::string_view s)
{
    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
    return converter.from_bytes(std::string{s});
}

} // namespace nix

#endif
