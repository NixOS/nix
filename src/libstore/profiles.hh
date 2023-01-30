#pragma once

#include "types.hh"
#include "pathlocks.hh"

#include <time.h>


namespace nix {

class StorePath;


typedef uint64_t GenerationNumber;

struct Generation
{
    GenerationNumber number;
    Path path;
    time_t creationTime;
};

typedef std::list<Generation> Generations;


/* Returns the list of currently present generations for the specified
   profile, sorted by generation number. Also returns the number of
   the current generation. */
std::pair<Generations, std::optional<GenerationNumber>> findGenerations(Path profile);

class LocalFSStore;

Path createGeneration(ref<LocalFSStore> store, Path profile, StorePath outPath);

void deleteGeneration(const Path & profile, GenerationNumber gen);

void deleteGenerations(const Path & profile, const std::set<GenerationNumber> & gensToDelete, bool dryRun);

void deleteGenerationsGreaterThan(const Path & profile, GenerationNumber max, bool dryRun);

void deleteOldGenerations(const Path & profile, bool dryRun);

void deleteGenerationsOlderThan(const Path & profile, time_t t, bool dryRun);

void deleteGenerationsOlderThan(const Path & profile, std::string_view timeSpec, bool dryRun);

void switchLink(Path link, Path target);

/* Roll back a profile to the specified generation, or to the most
   recent one older than the current. */
void switchGeneration(
    const Path & profile,
    std::optional<GenerationNumber> dstGen,
    bool dryRun);

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
std::string optimisticLockProfile(const Path & profile);

/* Creates and returns the path to a directory suitable for storing the user’s
   profiles. */
Path profilesDir();

/* Resolve ~/.nix-profile. If ~/.nix-profile doesn't exist yet, create
   it. */
Path getDefaultProfile();

}
