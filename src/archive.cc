#include <vector>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>

#include "archive.hh"
#include "util.hh"


static string archiveVersion1 = "nix-archive-1";


static void writePadding(unsigned int len, DumpSink & sink)
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
    writePadding(len, sink);
}


static void dump(const string & path, DumpSink & sink);


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
        writeString("node", sink);
        dump(path + "/" + *it, sink);
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
    if (fd == -1) throw SysError(format("opening file `%1%'") % path);
    
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

    writePadding(size, sink);

    close(fd); /* !!! close on exception */
}


static void dump(const string & path, DumpSink & sink)
{
    struct stat st;
    if (lstat(path.c_str(), &st))
        throw SysError(format("getting attributes of path `%1%'") % path);

    writeString("(", sink);

    if (S_ISREG(st.st_mode)) {
        writeString("type", sink);
        writeString("regular", sink);
        if (st.st_mode & S_IXUSR) {
            writeString("executable", sink);
            writeString("", sink);
        }
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


void dumpPath(const string & path, DumpSink & sink)
{
    writeString(archiveVersion1, sink);
    dump(path, sink);
}


static Error badArchive(string s)
{
    return Error("bad archive: " + s);
}


static void readPadding(unsigned int len, RestoreSource & source)
{
    if (len % 8) {
        unsigned char zero[8];
        unsigned int n = 8 - (len % 8);
        source(zero, n);
        for (unsigned int i = 0; i < n; i++)
            if (zero[i]) throw badArchive("non-zero padding");
    }
}

static unsigned int readInt(RestoreSource & source)
{
    unsigned char buf[8];
    source(buf, sizeof(buf));
    if (buf[4] || buf[5] || buf[6] || buf[7])
        throw Error("implementation cannot deal with > 32-bit integers");
    return
        buf[0] |
        (buf[1] << 8) |
        (buf[2] << 16) |
        (buf[3] << 24);
}


static string readString(RestoreSource & source)
{
    unsigned int len = readInt(source);
    char buf[len];
    source((unsigned char *) buf, len);
    readPadding(len, source);
    return string(buf, len);
}


static void skipGeneric(RestoreSource & source)
{
    if (readString(source) == "(") {
        while (readString(source) != ")")
            skipGeneric(source);
    }
}


static void restore(const string & path, RestoreSource & source);


static void restoreEntry(const string & path, RestoreSource & source)
{
    string s, name;

    s = readString(source);
    if (s != "(") throw badArchive("expected open tag");

    while (1) {
        s = readString(source);

        if (s == ")") {
            break;
        } else if (s == "name") {
            name = readString(source);
        } else if (s == "node") {
            if (s == "") throw badArchive("entry name missing");
            restore(path + "/" + name, source);
        } else {
            throw badArchive("unknown field " + s);
            skipGeneric(source);
        }
    }
}


static void restoreContents(int fd, const string & path, RestoreSource & source)
{
    unsigned int size = readInt(source);
    unsigned int left = size;
    unsigned char buf[65536];

    while (left) {
        unsigned int n = sizeof(buf);
        if (n > left) n = left;
        source(buf, n);
        if (write(fd, buf, n) != (ssize_t) n)
            throw SysError("writing file " + path);
        left -= n;
    }

    readPadding(size, source);
}


static void restore(const string & path, RestoreSource & source)
{
    string s;

    s = readString(source);
    if (s != "(") throw badArchive("expected open tag");

    enum { tpUnknown, tpRegular, tpDirectory, tpSymlink } type = tpUnknown;
    int fd = -1; /* !!! close on exception */

    while (1) {
        s = readString(source);

        if (s == ")") {
            break;
        }

        else if (s == "type") {
            if (type != tpUnknown)
                throw badArchive("multiple type fields");
            string t = readString(source);

            if (t == "regular") {
                type = tpRegular;
                fd = open(path.c_str(), O_CREAT | O_EXCL | O_WRONLY, 0666);
                if (fd == -1)
                    throw SysError("creating file " + path);
            }

            else if (t == "directory") {
                type = tpDirectory;
                if (mkdir(path.c_str(), 0777) == -1)
                    throw SysError("creating directory " + path);
            }

            else if (t == "symlink") {
                type = tpSymlink;
            }
            
            else throw badArchive("unknown file type " + t);
            
        }

        else if (s == "contents" && type == tpRegular) {
            restoreContents(fd, path, source);
        }

        else if (s == "executable" && type == tpRegular) {
            readString(source);
            struct stat st;
            if (fstat(fd, &st) == -1)
                throw SysError("fstat");
            if (fchmod(fd, st.st_mode | (S_IXUSR | S_IXGRP | S_IXOTH)) == -1)
                throw SysError("fchmod");
        }

        else if (s == "entry" && type == tpDirectory) {
            restoreEntry(path, source);
        }

        else if (s == "target" && type == tpSymlink) {
            string target = readString(source);
            if (symlink(target.c_str(), path.c_str()) == -1)
                throw SysError("creating symlink " + path);
        }

        else {
            throw badArchive("unknown field " + s);
            skipGeneric(source);
        }
        
    }

    if (fd != -1) close(fd);
}


void restorePath(const string & path, RestoreSource & source)
{
    if (readString(source) != archiveVersion1)
        throw badArchive("expected Nix archive");
    restore(path, source);
}

