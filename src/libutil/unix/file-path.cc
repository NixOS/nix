#include "nix/util/file-path.hh"

namespace nix {

std::filesystem::path toOwnedPath(PathView path)
{
    return {std::string{path}};
}

} // namespace nix
