#pragma once

#include "input-accessor.hh"

namespace nix {

struct GitRepo
{
    static ref<GitRepo> openRepo(const CanonPath & path);

    virtual uint64_t getRevCount(const Hash & rev) = 0;

    virtual uint64_t getLastModified(const Hash & rev) = 0;
};

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
