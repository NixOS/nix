#pragma once

#include "types.hh"
#include "pathlocks.hh"

#include <time.h>


namespace nix {


struct Generation
{
    int number;
    Path path;
    time_t creationTime;
    Generation()
    {
        number = -1;
    }
    operator bool() const
    {
        return number != -1;
    }
};

typedef list<Generation> Generations;


/* Returns the list of currently present generations for the specified
   profile, sorted by generation number. */
Generations findGenerations(Path profile, int & curGen);

class LocalFSStore;

Path createGeneration(ref<LocalFSStore> store, Path profile, Path outPath);

void deleteGeneration(const Path & profile, unsigned int gen);

void deleteGenerations(const Path & profile, const std::set<unsigned int> & gensToDelete, bool dryRun);

void deleteGenerationsGreaterThan(const Path & profile, const int max, bool dryRun);

void deleteOldGenerations(const Path & profile, bool dryRun);

void deleteGenerationsOlderThan(const Path & profile, time_t t, bool dryRun);

void deleteGenerationsOlderThan(const Path & profile, const string & timeSpec, bool dryRun);

void switchLink(Path link, Path target);

/* Ensure exclusive access to a profile.  Any command that modifies
   the profile first acquires this lock. */
void lockProfile(PathLocks & lock, const Path & profile);

/* Optimistic locking is used by long-running operations like `nix-env
   -i'.  Instead of acquiring the exclusive lock for the entire
   duration of the operation, we just perform the operation
   optimistically (without an exclusive lock), and check at the end
   whether the profile changed while we were busy (i.e., the symlink
   target changed).  If so, the operation is restarted.  Restarting is
   generally cheap, since the build results are still in the Nix
   store.  Most of the time, only the user environment has to be
   rebuilt. */
string optimisticLockProfile(const Path & profile);

}
