#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>

#include "references.hh"
#include "hash.hh"


static void search(const string & s,
    Strings & refs, Strings & seen)
{
    for (Strings::iterator i = refs.begin();
         i != refs.end(); )
    {
        if (s.find(*i) == string::npos)
            i++;
        else {
            debug(format("found reference to `%1%'") % *i);
            seen.push_back(*i);
            i = refs.erase(i);
        }
    }
}


void checkPath(const string & path,
    Strings & refs, Strings & seen)
{
    struct stat st;
    if (lstat(path.c_str(), &st))
        throw SysError(format("getting attributes of path `%1%'") % path);

    if (S_ISDIR(st.st_mode)) {
        DIR * dir = opendir(path.c_str());

        struct dirent * dirent;
        while (errno = 0, dirent = readdir(dir)) {
            string name = dirent->d_name;
            if (name == "." || name == "..") continue;
            search(name, refs, seen);
            checkPath(path + "/" + name, refs, seen);
        }

        closedir(dir); /* !!! close on exception */
    }

    else if (S_ISREG(st.st_mode)) {
        
        debug(format("checking `%1%'") % path);

        int fd = open(path.c_str(), O_RDONLY);
        if (fd == -1) throw SysError(format("opening file `%1%'") % path);

        char * buf = new char[st.st_size];

        if (read(fd, buf, st.st_size) != st.st_size)
            throw SysError(format("reading file %1%") % path);

        search(string(buf, st.st_size), refs, seen);
        
        delete buf; /* !!! autodelete */

        close(fd); /* !!! close on exception */
    }
    
    else if (S_ISLNK(st.st_mode)) {
        char buf[st.st_size];
        if (readlink(path.c_str(), buf, st.st_size) != st.st_size)
            throw SysError(format("reading symbolic link `%1%'") % path);
        search(string(buf, st.st_size), refs, seen);
    }
    
    else throw Error(format("unknown file type: %1%") % path);
}


Strings filterReferences(const string & path, const Strings & _refs)
{
    Strings refs;
    Strings seen;

    /* For efficiency (and a higher hit rate), just search for the
       hash part of the file name.  (This assumes that all references
       have the form `HASH-bla'). */
    for (Strings::const_iterator i = _refs.begin();
         i != _refs.end(); i++)
    {
        string s = string(baseNameOf(*i), 0, 32);
        parseHash(s);
        refs.push_back(s);
    }

    checkPath(path, refs, seen);

    return seen;
}
