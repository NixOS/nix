#include <cerrno>
#include <algorithm>
#include <vector>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>

#include "archive.hh"
#include "util.hh"


namespace nix {


static string archiveVersion1 = "nix-archive-1";


static void dump(const string & path, Sink & sink);


static void dumpEntries(const Path & path, Sink & sink)
{
    Strings names = readDirectory(path);
    vector<string> names2(names.begin(), names.end());
    sort(names2.begin(), names2.end());

    for (vector<string>::iterator it = names2.begin();
         it != names2.end(); it++)
    {
        writeString("entry", sink);
        writeString("(", sink);
        writeString("name", sink);
        writeString(*it, sink);
        writeString("node", sink);
        dump(path + "/" + *it, sink);
        writeString(")", sink);
    }
}


static void dumpContents(const Path & path, unsigned int size, 
    Sink & sink)
{
    writeString("contents", sink);
    writeInt(size, sink);

    AutoCloseFD fd = open(path.c_str(), O_RDONLY);
    if (fd == -1) throw SysError(format("opening file `%1%'") % path);
    
    unsigned char buf[65536];
    unsigned int left = size;

    while (left > 0) {
        size_t n = left > sizeof(buf) ? sizeof(buf) : left;
        readFull(fd, buf, n);
        left -= n;
        sink(buf, n);
    }

    writePadding(size, sink);
}


static void dump(const Path & path, Sink & sink)
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
        writeString("target", sink);
        writeString(readLink(path), sink);
    }

    else throw Error("unknown file type: " + path);

    writeString(")", sink);
}


void dumpPath(const Path & path, Sink & sink)
{
    writeString(archiveVersion1, sink);
    dump(path, sink);
}


static Error badArchive(string s)
{
    return Error("bad archive: " + s);
}


static void skipGeneric(Source & source)
{
    if (readString(source) == "(") {
        while (readString(source) != ")")
            skipGeneric(source);
    }
}


static void restore(const Path & path, Source & source);


static void restoreEntry(const Path & path, Source & source)
{
    string s, name;

    s = readString(source);
    if (s != "(") throw badArchive("expected open tag");

    while (1) {
        checkInterrupt();

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


static void restoreContents(int fd, const Path & path, Source & source)
{
    unsigned int size = readInt(source);
    unsigned int left = size;
    unsigned char buf[65536];

    while (left) {
        checkInterrupt();
        unsigned int n = sizeof(buf);
        if (n > left) n = left;
        source(buf, n);
        writeFull(fd, buf, n);
        left -= n;
    }

    readPadding(size, source);
}


static void restore(const Path & path, Source & source)
{
    string s;

    s = readString(source);
    if (s != "(") throw badArchive("expected open tag");

    enum { tpUnknown, tpRegular, tpDirectory, tpSymlink } type = tpUnknown;
    AutoCloseFD fd;

    while (1) {
        checkInterrupt();

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
}


void restorePath(const Path & path, Source & source)
{
    if (readString(source) != archiveVersion1)
        throw badArchive("expected Nix archive");
    restore(path, source);
}

 
}
