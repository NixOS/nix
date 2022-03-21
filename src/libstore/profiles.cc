#include "profiles.hh"
#include "store-api.hh"
#include "local-fs-store.hh"
#include "util.hh"

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>


namespace nix {


/* Parse a generation name of the format
   `<profilename>-<number>-link'. */
static std::optional<GenerationNumber> parseName(const std::string & profileName, const std::string & name)
{
    if (name.substr(0, profileName.size() + 1) != profileName + "-") return {};
    auto s = name.substr(profileName.size() + 1);
    auto p = s.find("-link");
    if (p == std::string::npos) return {};
    if (auto n = string2Int<unsigned int>(s.substr(0, p)))
        return *n;
    else
        return {};
}



std::pair<Generations, std::optional<GenerationNumber>> findGenerations(Path profile)
{
    Generations gens;

    Path profileDir = dirOf(profile);
    auto profileName = std::string(baseNameOf(profile));

    for (auto & i : readDirectory(profileDir)) {
        if (auto n = parseName(profileName, i.name)) {
            auto path = profileDir + "/" + i.name;
            gens.push_back({
                .number = *n,
                .path = path,
                .creationTime = lstat(path).st_mtime
            });
        }
    }

    gens.sort([](const Generation & a, const Generation & b)
    {
        return a.number < b.number;
    });

    return {
        gens,
        pathExists(profile)
        ? parseName(profileName, readLink(profile))
        : std::nullopt
    };
}


static void makeName(const Path & profile, GenerationNumber num,
    Path & outLink)
{
    Path prefix = (format("%1%-%2%") % profile % num).str();
    outLink = prefix + "-link";
}


Path createGeneration(ref<LocalFSStore> store, Path profile, StorePath outPath)
{
    /* The new generation number should be higher than old the
       previous ones. */
    auto [gens, dummy] = findGenerations(profile);

    GenerationNumber num;
    if (gens.size() > 0) {
        Generation last = gens.back();

        if (readLink(last.path) == store->printStorePath(outPath)) {
            /* We only create a new generation symlink if it differs
               from the last one.

               This helps keeping gratuitous installs/rebuilds from piling
               up uncontrolled numbers of generations, cluttering up the
               UI like grub. */
            return last.path;
        }

        num = gens.back().number;
    } else {
        num = 0;
    }

    /* Create the new generation.  Note that addPermRoot() blocks if
       the garbage collector is running to prevent the stuff we've
       built from moving from the temporary roots (which the GC knows)
       to the permanent roots (of which the GC would have a stale
       view).  If we didn't do it this way, the GC might remove the
       user environment etc. we've just built. */
    Path generation;
    makeName(profile, num + 1, generation);
    store->addPermRoot(outPath, generation);

    return generation;
}


static void removeFile(const Path & path)
{
    if (remove(path.c_str()) == -1)
        throw SysError("cannot unlink '%1%'", path);
}


void deleteGeneration(const Path & profile, GenerationNumber gen)
{
    Path generation;
    makeName(profile, gen, generation);
    removeFile(generation);
}


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
        if (!gensToDelete.count(i.number)) continue;
        deleteGeneration2(profile, i.number, dryRun);
    }
}

void deleteGenerationsGreaterThan(const Path & profile, GenerationNumber max, bool dryRun)
{
    PathLocks lock;
    lockProfile(lock, profile);

    bool fromCurGen = false;
    auto [gens, curGen] = findGenerations(profile);
    for (auto i = gens.rbegin(); i != gens.rend(); ++i) {
        if (i->number == curGen) {
            fromCurGen = true;
            max--;
            continue;
        }
        if (fromCurGen) {
            if (max) {
                max--;
                continue;
            }
            deleteGeneration2(profile, i->number, dryRun);
        }
    }
}

void deleteOldGenerations(const Path & profile, bool dryRun)
{
    PathLocks lock;
    lockProfile(lock, profile);

    auto [gens, curGen] = findGenerations(profile);

    for (auto & i : gens)
        if (i.number != curGen)
            deleteGeneration2(profile, i.number, dryRun);
}


void deleteGenerationsOlderThan(const Path & profile, time_t t, bool dryRun)
{
    PathLocks lock;
    lockProfile(lock, profile);

    auto [gens, curGen] = findGenerations(profile);

    bool canDelete = false;
    for (auto i = gens.rbegin(); i != gens.rend(); ++i)
        if (canDelete) {
            assert(i->creationTime < t);
            if (i->number != curGen)
                deleteGeneration2(profile, i->number, dryRun);
        } else if (i->creationTime < t) {
            /* We may now start deleting generations, but we don't
               delete this generation yet, because this generation was
               still the one that was active at the requested point in
               time. */
            canDelete = true;
        }
}


void deleteGenerationsOlderThan(const Path & profile, std::string_view timeSpec, bool dryRun)
{
    if (timeSpec.empty() || timeSpec[timeSpec.size() - 1] != 'd')
        throw UsageError("invalid number of days specifier '%1%', expected something like '14d'", timeSpec);

    time_t curTime = time(0);
    auto strDays = timeSpec.substr(0, timeSpec.size() - 1);
    auto days = string2Int<int>(strDays);

    if (!days || *days < 1)
        throw UsageError("invalid number of days specifier '%1%'", timeSpec);

    time_t oldTime = curTime - *days * 24 * 3600;

    deleteGenerationsOlderThan(profile, oldTime, dryRun);
}


void switchLink(Path link, Path target)
{
    /* Hacky. */
    if (dirOf(target) == dirOf(link)) target = baseNameOf(target);

    replaceSymlink(target, link);
}


void switchGeneration(
    const Path & profile,
    std::optional<GenerationNumber> dstGen,
    bool dryRun)
{
    PathLocks lock;
    lockProfile(lock, profile);

    auto [gens, curGen] = findGenerations(profile);

    std::optional<Generation> dst;
    for (auto & i : gens)
        if ((!dstGen && i.number < curGen) ||
            (dstGen && i.number == *dstGen))
            dst = i;

    if (!dst) {
        if (dstGen)
            throw Error("profile version %1% does not exist", *dstGen);
        else
            throw Error("no profile version older than the current (%1%) exists", curGen.value_or(0));
    }

    notice("switching profile from version %d to %d", curGen.value_or(0), dst->number);

    if (dryRun) return;

    switchLink(profile, dst->path);
}


void lockProfile(PathLocks & lock, const Path & profile)
{
    lock.lockPaths({profile}, (format("waiting for lock on profile '%1%'") % profile).str());
    lock.setDeletion(true);
}


std::string optimisticLockProfile(const Path & profile)
{
    return pathExists(profile) ? readLink(profile) : "";
}


Path getDefaultProfile()
{
    Path profileLink = getHome() + "/.nix-profile";
    try {
        if (!pathExists(profileLink)) {
            replaceSymlink(
                getuid() == 0
                ? settings.nixStateDir + "/profiles/default"
                : fmt("%s/profiles/per-user/%s/profile", settings.nixStateDir, getUserName()),
                profileLink);
        }
        return absPath(readLink(profileLink), dirOf(profileLink));
    } catch (Error &) {
        return profileLink;
    }
}


}
