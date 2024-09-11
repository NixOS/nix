#include <algorithm>
#include <codecvt>
#include <iostream>
#include <locale>

#include "file-path.hh"
#include "util.hh"

namespace nix {

std::optional<std::filesystem::path> maybePath(PathView path)
{
    return { path };
}

std::filesystem::path pathNG(PathView path)
{
    return path;
}

}
