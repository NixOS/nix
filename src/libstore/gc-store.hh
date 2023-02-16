#pragma once

#include "store-api.hh"

#include <future>


namespace nix {


typedef std::unordered_map<StorePath, std::unordered_set<std::string>> Roots;


struct GCOptions
{
    /* Garbage collector operation:

       - `gcReturnLive': return the set of paths reachable from
         (i.e. in the closure of) the roots.

       - `gcReturnDead': return the set of paths not reachable from
         the roots.

       - `gcDeleteDead': actually delete the latter set.

       - `gcDeleteSpecific': delete the paths listed in
          `pathsToDelete', insofar as they are not reachable.
    */
    typedef enum {
        gcReturnLive,
        gcReturnDead,
        gcDeleteDead,
        gcDeleteSpecific,
    } GCAction;

    GCAction action{gcDeleteDead};

    /* If `ignoreLiveness' is set, then reachability from the roots is
       ignored (dangerous!).  However, the paths must still be
       unreferenced *within* the store (i.e., there can be no other
       store paths that depend on them). */
    bool ignoreLiveness{false};

    /* For `gcDeleteSpecific', the paths to delete. */
    StorePathSet pathsToDelete;

    /* Stop after at least `maxFreed' bytes have been freed. */
    uint64_t maxFreed{std::numeric_limits<uint64_t>::max()};
};


struct GCResults
{
    /* Depending on the action, the GC roots, or the paths that would
       be or have been deleted. */
    PathSet paths;

    /* For `gcReturnDead', `gcDeleteDead' and `gcDeleteSpecific', the
       number of bytes that would be or was freed. */
    uint64_t bytesFreed = 0;
};


struct GcStore : public virtual Store
{
    inline static std::string operationName = "Garbage collection";

    ~GcStore();

    /* Add an indirect root, which is merely a symlink to `path' from
       /nix/var/nix/gcroots/auto/<hash of `path'>.  `path' is supposed
       to be a symlink to a store path.  The garbage collector will
       automatically remove the indirect root when it finds that
       `path' has disappeared. */
    virtual void addIndirectRoot(const Path & path) = 0;

    /* Find the roots of the garbage collector.  Each root is a pair
       (link, storepath) where `link' is the path of the symlink
       outside of the Nix store that point to `storePath'. If
       'censor' is true, privacy-sensitive information about roots
       found in /proc is censored. */
    virtual Roots findRoots(bool censor) = 0;

    /* Perform a garbage collection. */
    virtual void collectGarbage(const GCOptions & options, GCResults & results) = 0;

    /* Do a garbage collection that observes the policy configured by
       `gc-threshold`, `gc-limit`, etc.  */
    void doGC(bool sync = true);

    /* Perform an automatic garbage collection, if enabled. */
    void autoGC(bool sync = true);

    /* Return the amount of available disk space in this store. Used
       by autoGC(). */
    virtual uint64_t getAvailableSpace()
    {
        return std::numeric_limits<uint64_t>::max();
    }

private:

    struct State
    {
        /* The last time we checked whether to do an auto-GC, or an
           auto-GC finished. */
        std::chrono::time_point<std::chrono::steady_clock> lastGCCheck;

        /* Whether auto-GC is running. If so, get gcFuture to wait for
           the GC to finish. */
        bool gcRunning = false;
        std::shared_future<void> gcFuture;

        /* How much disk space was available after the previous
           auto-GC. If the current available disk space is below
           minFree but not much below availAfterGC, then there is no
           point in starting a new GC. */
        uint64_t availAfterGC = std::numeric_limits<uint64_t>::max();
    };

    Sync<State> _state;

};

}
