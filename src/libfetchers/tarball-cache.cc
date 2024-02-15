#include "tarball-cache.hh"
#include "users.hh"

namespace nix::fetchers {

ref<GitRepo> getTarballCache()
{
    static auto repoDir = std::filesystem::path(getCacheDir()) / "nix" / "tarball-cache";

    return GitRepo::openRepo(repoDir, true, true);
}

}
