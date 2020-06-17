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
Generations findGenerations(PathView profile, int & curGen);

class LocalFSStore;

Path createGeneration(ref<LocalFSStore> store, PathView profile, PathView outPath);

void deleteGeneration(PathView profile, unsigned int gen);

void deleteGenerations(PathView profile, const std::set<unsigned int> & gensToDelete, bool dryRun);

void deleteGenerationsGreaterThan(PathView profile, const int max, bool dryRun);

void deleteOldGenerations(PathView profile, bool dryRun);

void deleteGenerationsOlderThan(PathView profile, time_t t, bool dryRun);

void deleteGenerationsOlderThan(PathView profile, std::string_view timeSpec, bool dryRun);

void switchLink(PathView link, PathView target);

/* Ensure exclusive access to a profile.  Any command that modifies
   the profile first acquires this lock. */
void lockProfile(PathLocks & lock, PathView profile);

/* Optimistic locking is used by long-running operations like `nix-env
   -i'.  Instead of acquiring the exclusive lock for the entire
   duration of the operation, we just perform the operation
   optimistically (without an exclusive lock), and check at the end
   whether the profile changed while we were busy (i.e., the symlink
   target changed).  If so, the operation is restarted.  Restarting is
   generally cheap, since the build results are still in the Nix
   store.  Most of the time, only the user environment has to be
   rebuilt. */
string optimisticLockProfile(PathView profile);

/* Resolve ~/.nix-profile. If ~/.nix-profile doesn't exist yet, create
   it. */
Path getDefaultProfile();

}
