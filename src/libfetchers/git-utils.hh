#pragma once

#include "input-accessor.hh"

namespace nix {

struct GitRepo
{
    static ref<GitRepo> openRepo(const CanonPath & path);

    virtual uint64_t getRevCount(const Hash & rev) = 0;

    virtual uint64_t getLastModified(const Hash & rev) = 0;

    virtual bool isShallow() = 0;

    /* Return the commit hash to which a ref points. */
    virtual Hash resolveRef(std::string ref) = 0;

    struct WorkdirInfo
    {
        bool isDirty = false;

        /* The checked out commit, or nullopt if there are no commits
           in the repo yet. */
        std::optional<Hash> headRev;

        /* The ref to which HEAD points, if any. */
        std::optional<std::string> ref;

        /* All files in the working directory that are unchanged,
           modified or added, but excluding deleted files. */
        std::set<CanonPath> files;
    };

    virtual WorkdirInfo getWorkdirInfo() = 0;
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
