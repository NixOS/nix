#include "nix/store/local-store.hh"

namespace nix {

std::optional<uint64_t> LocalStore::killBuild(const StorePath & path)
{
    (void) path;
    throw UsageError("build termination is not supported on Windows");
}

} // namespace nix
