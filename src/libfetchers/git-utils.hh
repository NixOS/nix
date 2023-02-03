#pragma once

#include "input-accessor.hh"

namespace nix {

ref<InputAccessor> makeGitInputAccessor(const CanonPath & path, const Hash & rev);

struct TarballInfo
{
    Hash treeHash;
    time_t lastModified;
};

TarballInfo importTarball(Source & source);

ref<InputAccessor> makeTarballCacheAccessor(const Hash & rev);

bool tarballCacheContains(const Hash & treeHash);

}
