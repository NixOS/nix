#include <iostream>

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


void debug(string s)
{
    cerr << "debug: " << s << endl;
}
