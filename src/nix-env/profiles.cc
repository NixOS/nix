#include "profiles.hh"

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>


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
    int p = s.find("-link");
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


static void makeNames(const Path & profile, unsigned int num,
    Path & generation, Path & gcrootDrv, Path & gcrootClr)
{
    Path prefix = (format("%1%-%2%") % profile % num).str();
    generation = prefix + "-link";
    gcrootDrv = prefix + "-drv.gcroot";
    gcrootClr = prefix + "-clr.gcroot";
}


Path createGeneration(Path profile, Path outPath,
    Path drvPath, Path clrPath)
{
    /* The new generation number should be higher than old the
       previous ones. */
    int dummy;
    Generations gens = findGenerations(profile, dummy);
    unsigned int num = gens.size() > 0 ? gens.front().number : 0;
        
    /* Create the new generation. */
    Path generation, gcrootDrv, gcrootClr;

    while (1) {
        makeNames(profile, num, generation, gcrootDrv, gcrootClr);
        if (symlink(outPath.c_str(), generation.c_str()) == 0) break;
        if (errno != EEXIST)
            throw SysError(format("creating symlink `%1%'") % generation);
        /* Somebody beat us to it, retry with a higher number. */
        num++;
    }

    writeStringToFile(gcrootDrv, drvPath);
    writeStringToFile(gcrootClr, clrPath);

    return generation;
}


static void removeFile(const Path & path)
{
    if (remove(path.c_str()) == -1)
        throw SysError(format("cannot unlink `%1%'") % path);
}


void deleteGeneration(const Path & profile, unsigned int gen)
{
    Path generation, gcrootDrv, gcrootClr;
    makeNames(profile, gen, generation, gcrootDrv, gcrootClr);
    removeFile(generation);
    if (pathExists(gcrootClr)) removeFile(gcrootClr);
    if (pathExists(gcrootDrv)) removeFile(gcrootDrv);
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
