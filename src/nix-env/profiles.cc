#include "profiles.hh"

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>


static bool cmpGensByNumber(const Generation & a, const Generation & b)
{
    return a.number < b.number;
}


Generations findGenerations(Path profile)
{
    Generations gens;

    Path profileDir = dirOf(profile);
    string profileName = baseNameOf(profile);
    
    Strings names = readDirectory(profileDir);
    for (Strings::iterator i = names.begin(); i != names.end(); ++i) {
        if (string(*i, 0, profileName.size() + 1) != profileName + "-") continue;
        string s = string(*i, profileName.size() + 1);
        int p = s.find("-link");
        if (p == string::npos) continue;
        istringstream str(string(s, 0, p));
        unsigned int n;
        if (str >> n && str.eof()) {
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

    return gens;
}


Path createGeneration(Path profile, Path outPath, Path drvPath)
{
    /* The new generation number should be higher than old the
       previous ones. */
    Generations gens = findGenerations(profile);
    unsigned int num = gens.size() > 0 ? gens.front().number : 0;
        
    /* Create the new generation. */
    Path generation, gcrootSrc;

    while (1) {
        Path prefix = (format("%1%-%2%") % profile % num).str();
        generation = prefix + "-link";
        gcrootSrc = prefix + "-src.gcroot";
        if (symlink(outPath.c_str(), generation.c_str()) == 0) break;
        if (errno != EEXIST)
            throw SysError(format("creating symlink `%1%'") % generation);
        /* Somebody beat us to it, retry with a higher number. */
        num++;
    }

    writeStringToFile(gcrootSrc, drvPath);

    return generation;
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
