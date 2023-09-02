#include <algorithm>
#include <codecvt>
#include <iostream>
#include <locale>

#include "file-path.hh"
#include "file-path-impl.hh"
#include "util.hh"

namespace nix {

std::string os_string_to_string(PathViewNG::string_view path)
{
    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
    return converter.to_bytes(PathNG::string_type { path });
}

PathNG::string_type string_to_os_string(std::string_view s)
{
    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
    return converter.from_bytes(std::string { s });
}

std::optional<PathNG> maybePathNG(PathView path)
{
    if (path.length() >= 3 && (('A' <= path[0] && path[0] <= 'Z') || ('a' <= path[0] && path[0] <= 'z')) && path[1] == ':' && WindowsPathTrait<char>::isPathSep(path[2])) {
        PathNG::string_type sw = string_to_os_string(
            std::string { "\\\\?\\" } + path);
        std::replace(sw.begin(), sw.end(), '/', '\\');
        return sw;
    }
    if (path.length() >= 7 && path[0] == '\\' && path[1] == '\\' && (path[2] == '.' || path[2] == '?') && path[3] == '\\' &&
               ('A' <= path[4] && path[4] <= 'Z') && path[5] == ':' && WindowsPathTrait<char>::isPathSep(path[6])) {
        PathNG::string_type sw = string_to_os_string(path);
        std::replace(sw.begin(), sw.end(), '/', '\\');
        return sw;
    }
    return std::optional<PathNG::string_type>();
}

PathNG pathNG(PathView path)
{
    std::optional<PathNG::string_type> sw = maybePathNG(path);
    if (!sw) {
        // FIXME why are we not using the regular error handling?
        std::cerr << "invalid path for WinAPI call ["<<path<<"]"<<std::endl;
        _exit(111);
    }
    return *sw;
}

}
