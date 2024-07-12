#include "environment-posix-path.hh"
#include <unistd.h>
namespace nix {

bool isExecutableAmbient(const std::string & path)
{
    const char * cpath = path.c_str();
    return access(cpath, X_OK) == 0;
}

} // namespace nix
