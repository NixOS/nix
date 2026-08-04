/// Runtime versions of the third-parties libraries libfetchers adds on top of the
/// lower nix libraries' dependencies.

#include "nix/util/library-versions.hh"
#include "nix/util/fmt.hh"

#include <git2/global.h>

namespace nix {

static std::string libgit2Version()
{
    int major = 0, minor = 0, rev = 0;
    git_libgit2_version(&major, &minor, &rev);
    return fmt("%d.%d.%d", major, minor, rev);
}

static RegisterLibraryVersion rLibgit2{"libgit2", libgit2Version};

} // namespace nix
