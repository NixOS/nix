#include "file-system.hh"

namespace nix {

Descriptor openDirectory(const std::filesystem::path & path)
{
    return open(path.c_str(), O_RDONLY | O_DIRECTORY);
}

}
