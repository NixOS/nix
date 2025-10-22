#pragma once

#include "nix/fetchers/filtering-source-accessor.hh"
#include "nix/util/fs-sink.hh"

namespace nix {

namespace fetchers {
struct PublicKey;
struct Settings;
} // namespace fetchers

/**
 * A sink that writes into a Git repository. Note that nothing may be written
 * until `flush()` is called.
 */
struct GitFileSystemObjectSink : ExtendedFileSystemObjectSink
{
    /**
     * Flush builder and return a final Git hash.
     */
    virtual Hash flush() = 0;
};

struct GitRepo
{
    virtual ~GitRepo() {}

    static ref<GitRepo> openRepo(const std::filesystem::path & path, bool create = false, bool bare = false);

    virtual uint64_t getRevCount(const Hash & rev) = 0;

    virtual uint64_t getLastModified(const Hash & rev) = 0;

    virtual bool isShallow() = 0;

    /* Return the commit hash to which a ref points. */
    virtual Hash resolveRef(std::string ref) = 0;

    virtual void setRemote(const std::string & name, const std::string & url) = 0;

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

        /* All modified or added files. */
        std::set<CanonPath> dirtyFiles;

        /* The deleted files. */
        std::set<CanonPath> deletedFiles;

        /* The submodules listed in .gitmodules of this workdir. */
        std::vector<Submodule> submodules;
    };

    virtual WorkdirInfo getWorkdirInfo() = 0;

    static WorkdirInfo getCachedWorkdirInfo(const std::filesystem::path & path);

    /* Get the ref that HEAD points to. */
    virtual std::optional<std::string> getWorkdirRef() = 0;

    /**
     * Return the submodules of this repo at the indicated revision,
     * along with the revision of each submodule.
     */
    virtual std::vector<std::tuple<Submodule, Hash>> getSubmodules(const Hash & rev, bool exportIgnore) = 0;

    virtual std::string resolveSubmoduleUrl(const std::string & url) = 0;

    virtual bool hasObject(const Hash & oid) = 0;

    virtual ref<SourceAccessor> getAccessor(
        const Hash & rev,
        bool exportIgnore,
        std::string displayPrefix,
        bool smudgeLfs = false,
        bool applyFilters = false) = 0;

    virtual ref<SourceAccessor>
    getAccessor(const WorkdirInfo & wd, bool exportIgnore, MakeNotAllowedError makeNotAllowedError) = 0;

    virtual ref<GitFileSystemObjectSink> getFileSystemObjectSink() = 0;

    virtual void flush() = 0;

    virtual void fetch(const std::string & url, const std::string & refspec, bool shallow) = 0;

    /**
     * Verify that commit `rev` is signed by one of the keys in
     * `publicKeys`. Throw an error if it isn't.
     */
    virtual void verifyCommit(const Hash & rev, const std::vector<fetchers::PublicKey> & publicKeys) = 0;

    /**
     * Given a Git tree hash, compute the hash of its NAR
     * serialisation. This is memoised on-disk.
     */
    virtual Hash treeHashToNarHash(const fetchers::Settings & settings, const Hash & treeHash) = 0;

    /**
     * If the specified Git object is a directory with a single entry
     * that is a directory, return the ID of that object.
     * Otherwise, return the passed ID unchanged.
     */
    virtual Hash dereferenceSingletonDirectory(const Hash & oid) = 0;
};

ref<GitRepo> getTarballCache();

// A helper to ensure that the `git_*_free` functions get called.
template<auto del>
struct Deleter
{
    template<typename T>
    void operator()(T * p) const
    {
        del(p);
    };
};

// A helper to ensure that we don't leak objects returned by libgit2.
template<typename T>
struct Setter
{
    T & t;
    typename T::pointer p = nullptr;

    Setter(T & t)
        : t(t)
    {
    }

    ~Setter()
    {
        if (p)
            t = T(p);
    }

    operator typename T::pointer *()
    {
        return &p;
    }
};

/**
 * Checks that the string can be a valid git reference, branch or tag name.
 * Accepts shorthand references (one-level refnames are allowed), pseudorefs
 * like `HEAD`.
 *
 * @note This is a coarse test to make sure that the refname is at least something
 * that Git can make sense of.
 */
bool isLegalRefName(const std::string & refName);

} // namespace nix
