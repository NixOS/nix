#include <algorithm>
#include <codecvt>
#include <iostream>
#include <locale>

#include "nix/util/file-path.hh"
#include "nix/util/util.hh"

namespace nix {

std::filesystem::path pathNG(PathView path)
{
    return {std::string{path}};
}

} // namespace nix
