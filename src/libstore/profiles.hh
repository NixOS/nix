#pragma once
/**
 * @file Implementation of Profiles.
 *
 * See the manual for additional information.
 */

#include "types.hh"
#include "pathlocks.hh"

#include <time.h>


namespace nix {

class StorePath;


/**
 * A positive number identifying a generation for a given profile.
 *
 * Generation numbers are assigned sequentially. Each new generation is
 * assigned 1 + the current highest generation number.
 */
typedef uint64_t GenerationNumber;

/**
 * A generation is a revision of a profile.
 *
 * Each generation is a mapping (key-value pair) from an identifier
 * (`number`) to a store object (specified by `path`).
 */
struct Generation
{
    /**
     * The number of a generation is its unique identifier within the
     * profile.
     */
    GenerationNumber number;
    /**
     * The store path identifies the store object that is the contents
     * of the generation.
     *
     * These store paths / objects are not unique to the generation
     * within a profile. Nix tries to ensure successive generations have
     * distinct contents to avoid bloat, but nothing stops two
     * non-adjacent generations from having the same contents.
     *
     * @todo Use `StorePath` instead of `Path`?
     */
    Path path;

    /**
     * When the generation was created. This is extra metadata about the
     * generation used to make garbage collecting old generations more
     * human-friendly.
     */
    time_t creationTime;
};

/**
 * All the generations of a profile
 */
typedef std::list<Generation> Generations;


/**
 * Find all generations for the given profile.
 *
 * @param profile A profile specified by its name and location combined into a path.
 *
 * @return The pair of:
 *
 *   - The list of currently present generations for the specified profile,
 *     sorted by ascending generation number.
 *
 *   - The number of the current/active generation.
 *
 * Note that the current/active generation need not be the latest one.
 */
std::pair<Generations, std::optional<GenerationNumber>> findGenerations(Path profile);

class LocalFSStore;

/**
 * Create a new generation of the given profile
 *
 * If the previous generation (not the currently active one!) has a
 * distinct store object, a fresh generation number is mapped to the
 * given store object, referenced by path. Otherwise, the previous
 * generation is assumed.
 *
 * The behavior of reusing existing generations like this makes this
 * procedure idempotent. It also avoids clutter.
 */
Path createGeneration(LocalFSStore & store, Path profile, StorePath outPath);

/**
 * Unconditionally delete a generation
 *
 * @param profile A profile specified by its name and location combined into a path.
 *
 * @param gen The generation number specifying exactly which generation
 * to delete.
 *
 * Because there is no check of whether the generation to delete is
 * active, this is somewhat unsafe.
 *
 * @todo Should we expose this at all?
 */
void deleteGeneration(const Path & profile, GenerationNumber gen);

/**
 * Delete the given set of generations.
 *
 * @param profile The profile, specified by its name and location combined into a path, whose generations we want to delete.
 *
 * @param gensToDelete The generations to delete, specified by a set of
 * numbers.
 *
 * @param dryRun Log what would be deleted instead of actually doing
 * so.
 *
 * Trying to delete the currently active generation will fail, and cause
 * no generations to be deleted.
 */
void deleteGenerations(const Path & profile, const std::set<GenerationNumber> & gensToDelete, bool dryRun);

/**
 * Delete generations older than `max` passed the current generation.
 *
 * @param profile The profile, specified by its name and location combined into a path, whose generations we want to delete.
 *
 * @param max How many generations to keep up to the current one. Must
 * be at least 1 so we don't delete the current one.
 *
 * @param dryRun Log what would be deleted instead of actually doing
 * so.
 */
void deleteGenerationsGreaterThan(const Path & profile, GenerationNumber max, bool dryRun);

/**
 * Delete all generations other than the current one
 *
 * @param profile The profile, specified by its name and location combined into a path, whose generations we want to delete.
 *
 * @param dryRun Log what would be deleted instead of actually doing
 * so.
 */
void deleteOldGenerations(const Path & profile, bool dryRun);

/**
 * Delete generations older than `t`, except for the most recent one
 * older than `t`.
 *
 * @param profile The profile, specified by its name and location combined into a path, whose generations we want to delete.
 *
 * @param dryRun Log what would be deleted instead of actually doing
 * so.
 */
void deleteGenerationsOlderThan(const Path & profile, time_t t, bool dryRun);

/**
 * Parse a temp spec intended for `deleteGenerationsOlderThan()`.
 *
 * Throws an exception if `timeSpec` fails to parse.
 */
time_t parseOlderThanTimeSpec(std::string_view timeSpec);

/**
 * Smaller wrapper around `replaceSymlink` for replacing the current
 * generation of a profile. Does not enforce proper structure.
 *
 * @todo Always use `switchGeneration()` instead, and delete this.
 */
void switchLink(Path link, Path target);

/**
 * Roll back a profile to the specified generation, or to the most
 * recent one older than the current.
 */
void switchGeneration(
    const Path & profile,
    std::optional<GenerationNumber> dstGen,
    bool dryRun);

/**
 * Ensure exclusive access to a profile.  Any command that modifies
 * the profile first acquires this lock.
 */
void lockProfile(PathLocks & lock, const Path & profile);

/**
 * Optimistic locking is used by long-running operations like `nix-env
 * -i'.  Instead of acquiring the exclusive lock for the entire
 * duration of the operation, we just perform the operation
 * optimistically (without an exclusive lock), and check at the end
 * whether the profile changed while we were busy (i.e., the symlink
 * target changed).  If so, the operation is restarted.  Restarting is
 * generally cheap, since the build results are still in the Nix
 * store.  Most of the time, only the user environment has to be
 * rebuilt.
 */
std::string optimisticLockProfile(const Path & profile);

/**
 * Create and return the path to a directory suitable for storing the user’s
 * profiles.
 */
Path profilesDir();

/**
 * Return the path to the profile directory for root (but don't try creating it)
 */
Path rootProfilesDir();

/**
 * Create and return the path to the file used for storing the users's channels
 */
Path defaultChannelsDir();

/**
 * Return the path to the channel directory for root (but don't try creating it)
 */
Path rootChannelsDir();

/**
 * Resolve the default profile (~/.nix-profile by default,
 * $XDG_STATE_HOME/nix/profile if XDG Base Directory Support is enabled),
 * and create if doesn't exist
 */
Path getDefaultProfile();

}
