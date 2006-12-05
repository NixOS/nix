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
    
    Strings names = readDirectory(profileDir);
    for (Strings::iterator i = names.begin(); i != names.end(); ++i) {
        int n;
        if ((n = parseName(profileName, *i)) != -1) {
            Generation gen;
            gen.path = profileDir + "/" + *i;
            gen.number = n;
            struct stat st;
            if (lstat(gen.path.c_str(), &st) != 0)
                throw SysError(format("statting `%1%'") % gen.path);
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
    unsigned int num = gens.size() > 0 ? gens.back().number : 0;

    /* Create the new generation.  Note that addPermRoot() blocks if
       the garbage collector is running to prevent the stuff we've
       built from moving from the temporary roots (which the GC knows)
       to the permanent roots (of which the GC would have a stale
       view).  If we didn't do it this way, the GC might remove the
       user environment etc. we've just built. */
    Path generation;
    makeName(profile, num + 1, generation);
    addPermRoot(outPath, generation, false, true);

    return generation;
}


static void removeFile(const Path & path)
{
    if (remove(path.c_str()) == -1)
        throw SysError(format("cannot unlink `%1%'") % path);
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
    
    Path tmp = canonPath(dirOf(link) + "/.new_" + baseNameOf(link));
    if (symlink(target.c_str(), tmp.c_str()) != 0)
        throw SysError(format("creating symlink `%1%'") % tmp);
    /* The rename() system call is supposed to be essentially atomic
       on Unix.  That is, if we have links `current -> X' and
       `new_current -> Y', and we rename new_current to current, a
       process accessing current will see X or Y, but never a
       file-not-found or other error condition.  This is sufficient to
       atomically switch user environments. */
    if (rename(tmp.c_str(), link.c_str()) != 0)
        throw SysError(format("renaming `%1%' to `%2%'") % tmp % link);
}

 
}

