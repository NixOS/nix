#include <algorithm>
#include <codecvt>
#include <iostream>
#include <locale>

#include "file-path.hh"
#include "util.hh"

namespace nix {

std::string os_string_to_string(PathView::string_view path)
{
    return std::string { path };
}

std::filesystem::path::string_type string_to_os_string(std::string_view s)
{
    return std::string { s };
}

std::optional<std::filesystem::path> maybePath(PathView path)
{
    return Path { Path::string_type { path } };
}

std::filesystem::path pathNG(PathView path)
{
    return Path::string_type { path };
}

}
