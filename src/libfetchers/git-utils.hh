#pragma once

#include "filtering-input-accessor.hh"
#include "input-accessor.hh"

namespace nix {

namespace fetchers { struct PublicKey; }

struct GitRepo
{
    virtual ~GitRepo()
    { }

    static ref<GitRepo> openRepo(const CanonPath & path, bool create = false, bool bare = false);

    virtual uint64_t getRevCount(const Hash & rev) = 0;

    virtual uint64_t getLastModified(const Hash & rev) = 0;

    virtual bool isShallow() = 0;

    /* Return the commit hash to which a ref points. */
    virtual Hash resolveRef(std::string ref) = 0;

    /**
     * Info about a submodule.
     */
    struct Submodule
    {
        CanonPath path;
        std::string url;
        std::string branch;
    };

    struct WorkdirInfo
    {
        bool isDirty = false;

        /* The checked out commit, or nullopt if there are no commits
           in the repo yet. */
        std::optional<Hash> headRev;

        /* All files in the working directory that are unchanged,
           modified or added, but excluding deleted files. */
        std::set<CanonPath> files;

        /* The submodules listed in .gitmodules of this workdir. */
        std::vector<Submodule> submodules;
    };

    virtual WorkdirInfo getWorkdirInfo() = 0;

    /* Get the ref that HEAD points to. */
    virtual std::optional<std::string> getWorkdirRef() = 0;

    /**
     * Return the submodules of this repo at the indicated revision,
     * along with the revision of each submodule.
     */
    virtual std::vector<std::tuple<Submodule, Hash>> getSubmodules(const Hash & rev, bool exportIgnore) = 0;

    virtual std::string resolveSubmoduleUrl(
        const std::string & url,
        const std::string & base) = 0;

    struct TarballInfo
    {
        Hash treeHash;
        time_t lastModified;
    };

    virtual bool hasObject(const Hash & oid) = 0;

    virtual ref<InputAccessor> getAccessor(const Hash & rev, bool exportIgnore) = 0;

    virtual ref<InputAccessor> getAccessor(const WorkdirInfo & wd, bool exportIgnore, MakeNotAllowedError makeNotAllowedError) = 0;

    virtual void fetch(
        const std::string & url,
        const std::string & refspec,
        bool shallow) = 0;

    /**
     * Verify that commit `rev` is signed by one of the keys in
     * `publicKeys`. Throw an error if it isn't.
     */
    virtual void verifyCommit(
        const Hash & rev,
        const std::vector<fetchers::PublicKey> & publicKeys) = 0;
};

}
