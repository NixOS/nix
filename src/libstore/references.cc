#include <cerrno>
#include <map>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>

#include "references.hh"
#include "hash.hh"


static void search(const string & s,
    Strings & ids, Strings & seen)
{
    for (Strings::iterator i = ids.begin();
         i != ids.end(); )
    {
        if (s.find(*i) == string::npos)
            i++;
        else {
            debug(format("found reference to `%1%'") % *i);
            seen.push_back(*i);
            i = ids.erase(i);
        }
    }
}


void checkPath(const string & path,
    Strings & ids, Strings & seen)
{
    struct stat st;
    if (lstat(path.c_str(), &st))
        throw SysError(format("getting attributes of path `%1%'") % path);

    if (S_ISDIR(st.st_mode)) {
        Strings names = readDirectory(path);
	for (Strings::iterator i = names.begin(); i != names.end(); i++) {
            search(*i, ids, seen);
            checkPath(path + "/" + *i, ids, seen);
        }
    }

    else if (S_ISREG(st.st_mode)) {
        
        debug(format("checking `%1%'") % path);

        AutoCloseFD fd = open(path.c_str(), O_RDONLY);
        if (fd == -1) throw SysError(format("opening file `%1%'") % path);

        unsigned char * buf = new unsigned char[st.st_size];

        readFull(fd, buf, st.st_size);

        search(string((char *) buf, st.st_size), ids, seen);
        
        delete buf; /* !!! autodelete */
    }
    
    else if (S_ISLNK(st.st_mode)) {
        char buf[st.st_size];
        if (readlink(path.c_str(), buf, st.st_size) != st.st_size)
            throw SysError(format("reading symbolic link `%1%'") % path);
        search(string(buf, st.st_size), ids, seen);
    }
    
    else throw Error(format("unknown file type: %1%") % path);
}


Strings filterReferences(const string & path, const Strings & paths)
{
    map<string, string> backMap;
    Strings ids;
    Strings seen;

    /* For efficiency (and a higher hit rate), just search for the
       hash part of the file name.  (This assumes that all references
       have the form `HASH-bla'). */
    for (Strings::const_iterator i = paths.begin();
         i != paths.end(); i++)
    {
        string s = string(baseNameOf(*i), 0, 32);
        parseHash(s);
        ids.push_back(s);
        backMap[s] = *i;
    }

    checkPath(path, ids, seen);

    Strings found;
    for (Strings::iterator i = seen.begin(); i != seen.end(); i++)
    {
        map<string, string>::iterator j;
        if ((j = backMap.find(*i)) == backMap.end()) abort();
        found.push_back(j->second);
    }

    return found;
}
