#include <vector>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>

#include "archive.hh"
#include "util.hh"


static void pad(unsigned int len, DumpSink & sink)
{
    if (len % 8) {
        unsigned char zero[8];
        memset(zero, 0, sizeof(zero));
        sink(zero, 8 - (len % 8));
    }
}


static void writeInt(unsigned int n, DumpSink & sink)
{
    unsigned char buf[8];
    memset(buf, 0, sizeof(buf));
    buf[0] = n & 0xff;
    buf[1] = (n >> 8) & 0xff;
    buf[2] = (n >> 16) & 0xff;
    buf[3] = (n >> 24) & 0xff;
    sink(buf, sizeof(buf));
}


static void writeString(const string & s, DumpSink & sink)
{
    unsigned int len = s.length();
    writeInt(len, sink);
    sink((const unsigned char *) s.c_str(), len);
    pad(len, sink);
}


static void dumpEntries(const string & path, DumpSink & sink)
{
    DIR * dir = opendir(path.c_str());
    if (!dir) throw SysError("opening directory " + path);

    vector<string> names;

    struct dirent * dirent;
    while (errno = 0, dirent = readdir(dir)) {
        string name = dirent->d_name;
        if (name == "." || name == "..") continue;
        names.push_back(name);
    }
    if (errno) throw SysError("reading directory " + path);

    sort(names.begin(), names.end());

    for (vector<string>::iterator it = names.begin();
         it != names.end(); it++)
    {
        writeString("entry", sink);
        writeString("(", sink);
        writeString("name", sink);
        writeString(*it, sink);
        writeString("file", sink);
        dumpPath(path + "/" + *it, sink);
        writeString(")", sink);
    }
    
    closedir(dir); /* !!! close on exception */
}


static void dumpContents(const string & path, unsigned int size, 
    DumpSink & sink)
{
    writeString("contents", sink);
    writeInt(size, sink);

    int fd = open(path.c_str(), O_RDONLY);
    if (!fd) throw SysError("opening file " + path);
    
    unsigned char buf[65536];

    unsigned int total = 0;
    ssize_t n;
    while ((n = read(fd, buf, sizeof(buf)))) {
        if (n == -1) throw SysError("reading file " + path);
        total += n;
        sink(buf, n);
    }

    if (total != size)
        throw SysError("file changed while reading it: " + path);

    pad(size, sink);

    close(fd); /* !!! close on exception */
}


void dumpPath(const string & path, DumpSink & sink)
{
    struct stat st;
    if (lstat(path.c_str(), &st))
        throw SysError("getting attributes of path " + path);

    writeString("(", sink);

    if (S_ISREG(st.st_mode)) {
        writeString("type", sink);
        writeString("regular", sink);
        dumpContents(path, st.st_size, sink);
    } 

    else if (S_ISDIR(st.st_mode)) {
        writeString("type", sink);
        writeString("directory", sink);
        dumpEntries(path, sink);
    }

    else if (S_ISLNK(st.st_mode)) {
        writeString("type", sink);
        writeString("symlink", sink);
        char buf[st.st_size];
        if (readlink(path.c_str(), buf, st.st_size) != st.st_size)
            throw SysError("reading symbolic link " + path);
        writeString("target", sink);
        writeString(string(buf, st.st_size), sink);
    }

    else throw Error("unknown file type: " + path);

    writeString(")", sink);
}


void restorePath(const string & path, ReadSource & source)
{
}
