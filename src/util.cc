#include <iostream>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>

#include "util.hh"


string thisSystem = SYSTEM;


Error::Error(const format & f)
{
    err = f.str();
}


SysError::SysError(const format & f)
    : Error(format("%1%: %2%") % f.str() % strerror(errno))
{
}


string absPath(string path, string dir)
{
    if (path[0] != '/') {
        if (dir == "") {
            char buf[PATH_MAX];
            if (!getcwd(buf, sizeof(buf)))
                throw SysError("cannot get cwd");
            dir = buf;
        }
        path = dir + "/" + path;
    }
    return canonPath(path);
}


string canonPath(const string & path)
{
    char resolved[PATH_MAX];
    if (!realpath(path.c_str(), resolved))
        throw SysError(format("cannot canonicalise path `%1%'") % path);
    /* !!! check that this removes trailing slashes */
    return resolved;
}


string dirOf(string path)
{
    unsigned int pos = path.rfind('/');
    if (pos == string::npos)
        throw Error(format("invalid file name: %1%") % path);
    return string(path, 0, pos);
}


string baseNameOf(string path)
{
    unsigned int pos = path.rfind('/');
    if (pos == string::npos)
        throw Error(format("invalid file name %1% ") % path);
    return string(path, pos + 1);
}


void deletePath(string path)
{
    struct stat st;
    if (lstat(path.c_str(), &st))
        throw SysError(format("getting attributes of path %1%") % path);

    if (S_ISDIR(st.st_mode)) {
        DIR * dir = opendir(path.c_str());

        struct dirent * dirent;
        while (errno = 0, dirent = readdir(dir)) {
            string name = dirent->d_name;
            if (name == "." || name == "..") continue;
            deletePath(path + "/" + name);
        }

        closedir(dir); /* !!! close on exception */
    }

    if (remove(path.c_str()) == -1)
        throw SysError(format("cannot unlink %1%") % path);
}


static int nestingLevel = 0;


Nest::Nest(bool nest)
{
    this->nest = nest;
    if (nest) nestingLevel++;
}


Nest::~Nest()
{
    if (nest) nestingLevel--;
}


void msg(const format & f)
{
    string spaces;
    for (int i = 0; i < nestingLevel; i++)
        spaces += "  ";
    cerr << format("%1%%2%\n") % spaces % f.str();
}


void debug(const format & f)
{
    msg(format("debug: %1%") % f.str());
}
