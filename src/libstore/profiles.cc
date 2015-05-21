#include "profiles.hh"
#include "store-api.hh"
#include "util.hh"

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>


namespace nix {


static bool cmpGensByNumber(const Generation & a, const Generation & b)
{
    return a.number < b.number;
}


/* Parse a generation name of the format
   `<profilename>-<number>-link'. */
static int parseName(const string & profileName, const string & name)
{
    if (string(name, 0, profileName.size() + 1) != profileName + "-") return -1;
    string s = string(name, profileName.size() + 1);
    string::size_type p = s.find("-link");
    if (p == string::npos) return -1;
    int n;
    if (string2Int(string(s, 0, p), n) && n >= 0)
        return n;
    else
        return -1;
}



Generations findGenerations(Path profile, int & curGen)
{
    Generations gens;

    Path profileDir = dirOf(profile);
    string profileName = baseNameOf(profile);

    for (auto & i : readDirectory(profileDir)) {
        int n;
        if ((n = parseName(profileName, i.name)) != -1) {
            Generation gen;
            gen.path = profileDir + "/" + i.name;
            gen.number = n;
            struct stat st;
            if (lstat(gen.path.c_str(), &st) != 0)
                throw SysError(format("statting ‘%1%’") % gen.path);
            gen.creationTime = st.st_mtime;
            gens.push_back(gen);
        }
    }

    gens.sort(cmpGensByNumber);

    curGen = pathExists(profile)
        ? parseName(profileName, readLink(profile))
        : -1;

    return gens;
}


static void makeName(const Path & profile, unsigned int num,
    Path & outLink)
{
    Path prefix = (format("%1%-%2%") % profile % num).str();
    outLink = prefix + "-link";
}


Path createGeneration(Path profile, Path outPath)
{
    /* The new generation number should be higher than old the
       previous ones. */
    int dummy;
    Generations gens = findGenerations(profile, dummy);

    unsigned int num;
    if (gens.size() > 0) {
        Generation last = gens.back();

        if (readLink(last.path) == outPath) {
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
    addPermRoot(*store, outPath, generation, false, true);

    return generation;
}


static void removeFile(const Path & path)
{
    if (remove(path.c_str()) == -1)
        throw SysError(format("cannot unlink ‘%1%’") % path);
}


void deleteGeneration(const Path & profile, unsigned int gen)
{
    Path generation;
    makeName(profile, gen, generation);
    removeFile(generation);
}


void switchLink(Path link, Path target)
{
    /* Hacky. */
    if (dirOf(target) == dirOf(link)) target = baseNameOf(target);

    replaceSymlink(target, link);
}


void lockProfile(PathLocks & lock, const Path & profile)
{
    lock.lockPaths(singleton<PathSet>(profile),
        (format("waiting for lock on profile ‘%1%’") % profile).str());
    lock.setDeletion(true);
}


string optimisticLockProfile(const Path & profile)
{
    return pathExists(profile) ? readLink(profile) : "";
}


}
