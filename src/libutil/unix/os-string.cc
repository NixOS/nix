#include <algorithm>
#include <codecvt>
#include <iostream>
#include <locale>

#include "file-path.hh"
#include "util.hh"

namespace nix {

std::string os_string_to_string(PathViewNG::string_view path)
{
    return std::string{path};
}

std::filesystem::path::string_type string_to_os_string(std::string_view s)
{
    return std::string{s};
}

}
