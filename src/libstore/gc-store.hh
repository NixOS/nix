#pragma once
///@file

#include "store-api.hh"


namespace nix {


typedef std::unordered_map<StorePath, std::unordered_set<std::string>> Roots;


struct GCOptions
{
    /**
     * Garbage collector operation:
     *
     * - `gcReturnLive`: return the set of paths reachable from
     *   (i.e. in the closure of) the roots.
     *
     * - `gcReturnDead`: return the set of paths not reachable from
     *   the roots.
     *
     * - `gcDeleteDead`: actually delete the latter set.
     *
     * - `gcDeleteSpecific`: delete all the paths, and fail if one of them
     *   isn't dead.
     */
    typedef enum {
        gcReturnLive,
        gcReturnDead,
        gcDeleteDead,
        gcDeleteSpecific,
    } GCAction;

    GCAction action{gcDeleteDead};

    /**
     * If `ignoreLiveness` is set, then reachability from the roots is
     * ignored (dangerous!).  However, the paths must still be
     * unreferenced *within* the store (i.e., there can be no other
     * store paths that depend on them).
     */
    bool ignoreLiveness{false};

    /**
     * The paths from which to delete.
     * If empty, and `action` is not `gcDeleteSpecific`, act on the whole store.
     */
    StorePathSet pathsToDelete;

    /**
     * Stop after at least `maxFreed` bytes have been freed.
     */
    uint64_t maxFreed{std::numeric_limits<uint64_t>::max()};
};


struct GCResults
{
    /**
     * Depending on the action, the GC roots, or the paths that would
     * be or have been deleted.
     */
    PathSet paths;

    /**
     * For `gcReturnDead`, `gcDeleteDead` and `gcDeleteSpecific`, the
     * number of bytes that would be or was freed.
     */
    uint64_t bytesFreed = 0;
};


/**
 * Mix-in class for \ref Store "stores" which expose a notion of garbage
 * collection.
 *
 * Garbage collection will allow deleting paths which are not
 * transitively "rooted".
 *
 * The notion of GC roots actually not part of this class.
 *
 *  - The base `Store` class has `Store::addTempRoot()` because for a store
 *    that doesn't support garbage collection at all, a temporary GC root is
 *    safely implementable as no-op.
 *
 *    @todo actually this is not so good because stores are *views*.
 *    Some views have only a no-op temp roots even though others to the
 *    same store allow triggering GC. For instance one can't add a root
 *    over ssh, but that doesn't prevent someone from gc-ing that store
 *    accesed via SSH locally).
 *
 *  - The derived `LocalFSStore` class has `LocalFSStore::addPermRoot`,
 *    which is not part of this class because it relies on the notion of
 *    an ambient file system. There are stores (`ssh-ng://`, for one),
 *    that *do* support garbage collection but *don't* expose any file
 *    system, and `LocalFSStore::addPermRoot` thus does not make sense
 *    for them.
 */
struct GcStore : public virtual Store
{
    inline static std::string operationName = "Garbage collection";

    /**
     * Find the roots of the garbage collector.  Each root is a pair
     * `(link, storepath)` where `link` is the path of the symlink
     * outside of the Nix store that point to `storePath`. If
     * `censor` is true, privacy-sensitive information about roots
     * found in `/proc` is censored.
     */
    virtual Roots findRoots(bool censor) = 0;

    /**
     * Perform a garbage collection.
     */
    virtual void collectGarbage(const GCOptions & options, GCResults & results) = 0;
};

}
