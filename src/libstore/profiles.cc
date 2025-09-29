#include "nix/store/profiles.hh"
#include "nix/util/signals.hh"
#include "nix/store/globals.hh"
#include "nix/store/store-api.hh"
#include "nix/store/local-fs-store.hh"
#include "nix/util/users.hh"

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <algorithm>

namespace nix {

/**
 * Parse a generation name of the format
 * `<profilename>-<number>-link'.
 */
static std::optional<GenerationNumber> parseName(const std::string & profileName, const std::string & name)
{
    if (name.substr(0, profileName.size() + 1) != profileName + "-")
        return {};
    auto s = name.substr(profileName.size() + 1);
    auto p = s.find("-link");
    if (p == std::string::npos)
        return {};
    if (auto n = string2Int<unsigned int>(s.substr(0, p)))
        return *n;
    else
        return {};
}

std::pair<Generations, std::optional<GenerationNumber>> findGenerations(Path profile)
{
    Generations gens;

    std::filesystem::path profileDir = dirOf(profile);
    auto profileName = std::string(baseNameOf(profile));

    for (auto & i : DirectoryIterator{profileDir}) {
        checkInterrupt();
        if (auto n = parseName(profileName, i.path().filename().string())) {
            auto path = i.path().string();
            gens.push_back({.number = *n, .path = path, .creationTime = lstat(path).st_mtime});
        }
    }

    gens.sort([](const Generation & a, const Generation & b) { return a.number < b.number; });

    return {gens, pathExists(profile) ? parseName(profileName, readLink(profile)) : std::nullopt};
}

/**
 * Create a generation name that can be parsed by `parseName()`.
 */
static Path makeName(const Path & profile, GenerationNumber num)
{
    return fmt("%s-%s-link", profile, num);
}

Path createGeneration(LocalFSStore & store, Path profile, StorePath outPath)
{
    /* The new generation number should be higher than old the
       previous ones. */
    auto [gens, dummy] = findGenerations(profile);

    GenerationNumber num;
    if (gens.size() > 0) {
        Generation last = gens.back();

        if (readLink(last.path) == store.printStorePath(outPath)) {
            /* We only create a new generation symlink if it differs
               from the last one.

               This helps keeping gratuitous installs/rebuilds from piling
               up uncontrolled numbers of generations, cluttering up the
               UI like grub. */
            return last.path;
        }

        num = last.number;
    } else {
        num = 0;
    }

    /* Create the new generation.  Note that addPermRoot() blocks if
       the garbage collector is running to prevent the stuff we've
       built from moving from the temporary roots (which the GC knows)
       to the permanent roots (of which the GC would have a stale
       view).  If we didn't do it this way, the GC might remove the
       user environment etc. we've just built. */
    Path generation = makeName(profile, num + 1);
    store.addPermRoot(outPath, generation);

    return generation;
}

static void removeFile(const Path & path)
{
    if (remove(path.c_str()) == -1)
        throw SysError("cannot unlink '%1%'", path);
}

void deleteGeneration(const Path & profile, GenerationNumber gen)
{
    Path generation = makeName(profile, gen);
    removeFile(generation);
}

/**
 * Delete a generation with dry-run mode.
 *
 * Like `deleteGeneration()` but:
 *
 *  - We log what we are going to do.
 *
 *  - We only actually delete if `dryRun` is false.
 */
static void deleteGeneration2(const Path & profile, GenerationNumber gen, bool dryRun)
{
    if (dryRun)
        notice("would remove profile version %1%", gen);
    else {
        notice("removing profile version %1%", gen);
        deleteGeneration(profile, gen);
    }
}

void deleteGenerations(const Path & profile, const std::set<GenerationNumber> & gensToDelete, bool dryRun)
{
    PathLocks lock;
    lockProfile(lock, profile);

    auto [gens, curGen] = findGenerations(profile);

    if (gensToDelete.count(*curGen))
        throw Error("cannot delete current version of profile %1%'", profile);

    for (auto & i : gens) {
        if (!gensToDelete.count(i.number))
            continue;
        deleteGeneration2(profile, i.number, dryRun);
    }
}

/**
 * Advance the iterator `count` times.
 */
static inline void iterDrop(Generations & gens, auto && i, GenerationNumber count = 1)
{
    for (GenerationNumber keep = 0; i != gens.rend() && keep < count; ++i, ++keep)
        ;
}

/**
 * Advance the iterator until the given predicate `cond` returns `true`.
 */
static inline void iterDropUntil(Generations & gens, auto && i, auto && cond)
{
    for (; i != gens.rend() && !cond(*i); ++i)
        ;
}

void deleteGenerationsFilter(
    const Path & profile,
    std::optional<time_t> olderThan,
    std::optional<GenerationNumber> keepMin,
    std::optional<GenerationNumber> keepMax,
    bool dryRun)
{
    if (keepMin.has_value() && keepMax.has_value() && *keepMin > *keepMax)
        throw Error("--keep-min cannot be greater than --keep-max");

    PathLocks lock;
    lockProfile(lock, profile);

    auto [gens, curGen] = findGenerations(profile);

    // Keep current and future generations
    auto current = gens.rbegin();
    iterDropUntil(gens, current, [&](auto & g) { return g.number == curGen; });
    iterDrop(gens, current);

    // Compute minimum bound for kept generations
    auto start = current;
    if (keepMin.has_value())
        iterDrop(gens, start, *keepMin);

    // Compute maximum bound for kept generations
    auto end = gens.rend();
    if (keepMax.has_value()) {
        end = current;
        iterDrop(gens, end, *keepMax);
    }

    // Find the first older generation, if one exists
    auto older = gens.rend();
    if (olderThan.has_value()) {
        older = current;
        iterDropUntil(gens, older, [&](auto & g) { return g.creationTime < *olderThan; });
        /* Take the previous generation

           We don't want delete this one yet because it
           existed at the requested point in time, and
           we want to be able to roll back to it. */
        iterDrop(gens, older);
    }

    // Find first generation to delete by clamping between keepMin and keepMax
    auto toDelete = older;

    auto clampBackward = std::distance(gens.rbegin(), older) - std::distance(gens.rbegin(), end);
    for (int i = clampBackward; i > 0; --i)
        --toDelete;

    auto clampForward = std::distance(gens.rbegin(), start) - std::distance(gens.rbegin(), older);
    for (int i = clampForward; i > 0; --i)
        ++toDelete;

    // Delete
    for (; toDelete != gens.rend(); ++toDelete)
        deleteGeneration2(profile, toDelete->number, dryRun);
}

void deleteGenerationsGreaterThan(const Path & profile, GenerationNumber max, bool dryRun)
{
    deleteGenerationsFilter(profile, std::nullopt, std::nullopt, std::optional(max), dryRun);
}

void deleteOldGenerations(const Path & profile, bool dryRun)
{
    deleteGenerationsFilter(profile, std::nullopt, std::nullopt, std::optional(0), dryRun);
}

void deleteGenerationsOlderThan(const Path & profile, time_t t, bool dryRun)
{
    deleteGenerationsFilter(profile, std::optional(t), std::nullopt, std::nullopt, dryRun);
}

time_t parseOlderThanTimeSpec(std::string_view timeSpec)
{
    if (timeSpec.empty() || timeSpec[timeSpec.size() - 1] != 'd')
        throw UsageError("invalid number of days specifier '%1%', expected something like '14d'", timeSpec);

    time_t curTime = time(0);
    auto strDays = timeSpec.substr(0, timeSpec.size() - 1);
    auto days = string2Int<int>(strDays);

    if (!days || *days < 1)
        throw UsageError("invalid number of days specifier '%1%'", timeSpec);

    return curTime - *days * 24 * 3600;
}

void switchLink(Path link, Path target)
{
    /* Hacky. */
    if (dirOf(target) == dirOf(link))
        target = baseNameOf(target);

    replaceSymlink(target, link);
}

void switchGeneration(const Path & profile, std::optional<GenerationNumber> dstGen, bool dryRun)
{
    PathLocks lock;
    lockProfile(lock, profile);

    auto [gens, curGen] = findGenerations(profile);

    std::optional<Generation> dst;
    for (auto & i : gens)
        if ((!dstGen && i.number < curGen) || (dstGen && i.number == *dstGen))
            dst = i;

    if (!dst) {
        if (dstGen)
            throw Error("profile version %1% does not exist", *dstGen);
        else
            throw Error("no profile version older than the current (%1%) exists", curGen.value_or(0));
    }

    notice("switching profile from version %d to %d", curGen.value_or(0), dst->number);

    if (dryRun)
        return;

    switchLink(profile, dst->path);
}

void lockProfile(PathLocks & lock, const Path & profile)
{
    lock.lockPaths({profile}, fmt("waiting for lock on profile '%1%'", profile));
    lock.setDeletion(true);
}

std::string optimisticLockProfile(const Path & profile)
{
    return pathExists(profile) ? readLink(profile) : "";
}

Path profilesDir()
{
    auto profileRoot = isRootUser() ? rootProfilesDir() : createNixStateDir() + "/profiles";
    createDirs(profileRoot);
    return profileRoot;
}

Path rootProfilesDir()
{
    return settings.nixStateDir + "/profiles/per-user/root";
}

Path getDefaultProfile()
{
    Path profileLink = settings.useXDGBaseDirectories ? createNixStateDir() + "/profile" : getHome() + "/.nix-profile";
    try {
        auto profile = profilesDir() + "/profile";
        if (!pathExists(profileLink)) {
            replaceSymlink(profile, profileLink);
        }
        // Backwards compatibility measure: Make root's profile available as
        // `.../default` as it's what NixOS and most of the init scripts expect
        Path globalProfileLink = settings.nixStateDir + "/profiles/default";
        if (isRootUser() && !pathExists(globalProfileLink)) {
            replaceSymlink(profile, globalProfileLink);
        }
        return absPath(readLink(profileLink), dirOf(profileLink));
    } catch (Error &) {
        return profileLink;
    } catch (std::filesystem::filesystem_error &) {
        return profileLink;
    }
}

Path defaultChannelsDir()
{
    return profilesDir() + "/channels";
}

Path rootChannelsDir()
{
    return rootProfilesDir() + "/channels";
}

} // namespace nix
