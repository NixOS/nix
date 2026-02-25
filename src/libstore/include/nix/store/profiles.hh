#pragma once
/**
 * @file
 *
 * Implementation of Profiles.
 *
 * See the manual for additional information.
 */

#include <filesystem>
#include <optional>
#include <time.h>

#include "nix/util/types.hh"
#include "nix/store/pathlocks.hh"

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
     * @todo Use `StorePath` instead of `std::filesystem::path`?
     */
    std::filesystem::path path;

    /**
     * When the generation was created. This is extra metadata about the
     * generation used to make garbage collecting old generations more
     * convenient.
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
 * @param profile A profile specified by its name and location combined
 * into a path. E.g. if "foo" is the name of the profile, and "/bar/baz"
 * is the directory it is in, then the path "/bar/baz/foo" would be the
 * argument for this parameter.
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
std::pair<Generations, std::optional<GenerationNumber>> findGenerations(std::filesystem::path profile);

struct LocalFSStore;

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
std::filesystem::path createGeneration(LocalFSStore & store, std::filesystem::path profile, StorePath outPath);

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
void deleteGeneration(const std::filesystem::path & profile, GenerationNumber gen);

/**
 * Delete the given set of generations.
 *
 * @param profile The profile, specified by its name and location combined into a path, whose generations we want to
 * delete.
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
void deleteGenerations(
    const std::filesystem::path & profile, const std::set<GenerationNumber> & gensToDelete, bool dryRun);

/**
 * Delete old generations. Will never delete the current or future generations.
 *
 * Examples:
 * - All parameters are nullopt
 *   No generations are deleted.
 * - keepMin is 5
 *   No generations are deleted, only keepMax and olderThan delete generations.
 * - keepMax is 10
 *   10 most recent generations after the current one are kept, the rest is deleted.
 * - olderThan is 2025-09-16
 *   Generations older than 2025-09-16 are deleted.
 * - olderThan is 2025-09-16, keepMin is 5, keepMax is 10 -
 *   Will try to delete generations older than 2025-09-16.
 *   If there are more than 10 generations to be kept, continues to delete old generations until there are 10.
 *   If there are less than 5 generations to be kept, preserves the most recent of generations to be deleted until there
 *   are 5.
 *
 * @param profile The profile, specified by its name and location combined into a path, whose generations we want to
 * delete.
 *
 * @param olderThan Age of the oldest generation to keep.
 * If nullopt, no generation will be deleted based on its age.
 *
 * @param keepMin Minimum amount of recent generations to keep after deletion (not counting the current or future ones).
 * If nullopt, all old generations will be deleted.
 *
 * @param keepMax Maximum amount of recent generations to keep after deletion (not counting the current or future ones).
 * If nullopt, all recent generations will be kept.
 *
 * @param dryRun Log what would be deleted instead of actually doing so.
 */
void deleteGenerationsFilter(
    const std::filesystem::path & profile,
    std::optional<time_t> olderThan,
    std::optional<GenerationNumber> keepMin,
    std::optional<GenerationNumber> keepMax,
    bool dryRun);

/**
 * Delete generations older than `max` passed the current generation.
 *
 * @param profile The profile, specified by its name and location combined into a path, whose generations we want to
 * delete.
 *
 * @param max How many generations to keep up to the current one. Must
 * be at least 1 so we don't delete the current one.
 *
 * @param dryRun Log what would be deleted instead of actually doing
 * so.
 */
void deleteGenerationsGreaterThan(const std::filesystem::path & profile, GenerationNumber max, bool dryRun);

/**
 * Delete all generations other than the current one
 *
 * @param profile The profile, specified by its name and location combined into a path, whose generations we want to
 * delete.
 *
 * @param dryRun Log what would be deleted instead of actually doing
 * so.
 */
void deleteOldGenerations(const std::filesystem::path & profile, bool dryRun);

/**
 * Delete generations older than `t`, except for the most recent one
 * older than `t`.
 *
 * @param profile The profile, specified by its name and location combined into a path, whose generations we want to
 * delete.
 *
 * @param dryRun Log what would be deleted instead of actually doing
 * so.
 */
void deleteGenerationsOlderThan(const std::filesystem::path & profile, time_t t, bool dryRun);

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
void switchLink(std::filesystem::path link, std::filesystem::path target);

/**
 * Roll back a profile to the specified generation, or to the most
 * recent one older than the current.
 */
void switchGeneration(const std::filesystem::path & profile, std::optional<GenerationNumber> dstGen, bool dryRun);

/**
 * Ensure exclusive access to a profile.  Any command that modifies
 * the profile first acquires this lock.
 */
void lockProfile(PathLocks & lock, const std::filesystem::path & profile);

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
std::string optimisticLockProfile(const std::filesystem::path & profile);

struct ProfileDirsOptions
{
    const std::filesystem::path & nixStateDir;
    bool useXDGBaseDirectories;
};

/**
 * Create and return the path to a directory suitable for storing the user’s
 * profiles.
 */
std::filesystem::path profilesDir(ProfileDirsOptions opts);

/**
 * Return the path to the profile directory for root (but don't try creating it)
 */
std::filesystem::path rootProfilesDir(ProfileDirsOptions opts);

/**
 * Create and return the path to the file used for storing the users's channels
 */
std::filesystem::path defaultChannelsDir(ProfileDirsOptions opts);

/**
 * Return the path to the channel directory for root (but don't try creating it)
 */
std::filesystem::path rootChannelsDir(ProfileDirsOptions opts);

/**
 * Resolve the default profile (~/.nix-profile by default,
 * $XDG_STATE_HOME/nix/profile if XDG Base Directory Support is enabled),
 * and create if doesn't exist
 */
std::filesystem::path getDefaultProfile(ProfileDirsOptions opts);

} // namespace nix
