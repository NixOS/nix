#include "nix/util/file-path.hh"

#include <filesystem>
#include <string>

namespace nix {

std::filesystem::path toOwnedPath(PathView path)
{
    return {std::string{path}};
}

} // namespace nix
