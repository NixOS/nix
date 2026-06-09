#include <algorithm>
#include <codecvt>
#include <iostream>
#include <locale>

#include "nix/util/file-path.hh"
#include "nix/util/file-path-impl.hh"
#include "nix/util/util.hh"

namespace nix {

std::optional<std::filesystem::path> maybePath(PathView path)
{
    if (path.length() >= 3 && (('A' <= path[0] && path[0] <= 'Z') || ('a' <= path[0] && path[0] <= 'z'))
        && path[1] == ':' && WindowsPathTrait<char>::isPathSep(path[2])) {
        std::filesystem::path::string_type sw = std::wstring{L"\\\\?\\"} + std::wstring{path};
        std::replace(sw.begin(), sw.end(), '/', '\\');
        return sw;
    }
    if (path.length() >= 7 && path[0] == '\\' && path[1] == '\\' && (path[2] == '.' || path[2] == '?')
        && path[3] == '\\' && ('A' <= path[4] && path[4] <= 'Z') && path[5] == ':'
        && WindowsPathTrait<char>::isPathSep(path[6])) {
        std::filesystem::path::string_type sw{path};
        std::replace(sw.begin(), sw.end(), '/', '\\');
        return sw;
    }
    return std::optional<std::filesystem::path::string_type>();
}

std::filesystem::path toOwnedPath(PathView path)
{
    std::optional<std::filesystem::path::string_type> sw = maybePath(path);
    if (!sw) {
        // FIXME why are we not using the regular error handling?
        std::wcerr << L"invalid path for WinAPI call [" << path << L"]" << std::endl;
        _exit(111);
    }
    return *sw;
}

} // namespace nix
