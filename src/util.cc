#include <iostream>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>

#include "util.hh"


string thisSystem = SYSTEM;


SysError::SysError(string msg)
{
    char * sysMsg = strerror(errno);
    err = msg + ": " + sysMsg;
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
        /* !!! canonicalise */
        char resolved[PATH_MAX];
        if (!realpath(path.c_str(), resolved))
            throw SysError("cannot canonicalise path " + path);
        path = resolved;
    }
    return path;
}


string dirOf(string path)
{
    unsigned int pos = path.rfind('/');
    if (pos == string::npos) throw Error("invalid file name: " + path);
    return string(path, 0, pos);
}


string baseNameOf(string path)
{
    unsigned int pos = path.rfind('/');
    if (pos == string::npos) throw Error("invalid file name: " + path);
    return string(path, pos + 1);
}


void deletePath(string path)
{
    struct stat st;
    if (lstat(path.c_str(), &st))
        throw SysError("getting attributes of path " + path);

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
        throw SysError("cannot unlink " + path);
}


void debug(const format & f)
{
    cerr << format("debug: %1%\n") % f.str();
}
