#include <cerrno>
#include <algorithm>
#include <vector>

#define _XOPEN_SOURCE 600
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>

#include "archive.hh"
#include "util.hh"

#include "config.h"


namespace nix {


static string archiveVersion1 = "nix-archive-1";


PathFilter defaultPathFilter;


static void dump(const string & path, Sink & sink, PathFilter & filter);


static void dumpEntries(const Path & path, Sink & sink, PathFilter & filter)
{
    Strings names = readDirectory(path);
    vector<string> names2(names.begin(), names.end());
    sort(names2.begin(), names2.end());

    for (vector<string>::iterator i = names2.begin();
         i != names2.end(); ++i)
    {
        Path entry = path + "/" + *i;
        if (filter(entry)) {
            writeString("entry", sink);
            writeString("(", sink);
            writeString("name", sink);
            writeString(*i, sink);
            writeString("node", sink);
            dump(entry, sink, filter);
            writeString(")", sink);
        }
    }
}


static void dumpContents(const Path & path, off_t size, 
    Sink & sink)
{
    writeString("contents", sink);
    writeLongLong(size, sink);

    AutoCloseFD fd = open(path.c_str(), O_RDONLY);
    if (fd == -1) throw SysError(format("opening file `%1%'") % path);
    
    unsigned char buf[65536];
    off_t left = size;

    while (left > 0) {
        size_t n = left > sizeof(buf) ? sizeof(buf) : left;
        readFull(fd, buf, n);
        left -= n;
        sink(buf, n);
    }

    writePadding(size, sink);
}


static void dump(const Path & path, Sink & sink, PathFilter & filter)
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
        dumpEntries(path, sink, filter);
    }

    else if (S_ISLNK(st.st_mode)) {
        writeString("type", sink);
        writeString("symlink", sink);
        writeString("target", sink);
        writeString(readLink(path), sink);
    }

    else throw Error(format("file `%1%' has an unknown type") % path);

    writeString(")", sink);
}


void dumpPath(const Path & path, Sink & sink, PathFilter & filter)
{
    writeString(archiveVersion1, sink);
    dump(path, sink, filter);
}


static SerialisationError badArchive(string s)
{
    return SerialisationError("bad archive: " + s);
}


static void skipGeneric(Source & source)
{
    if (readString(source) == "(") {
        while (readString(source) != ")")
            skipGeneric(source);
    }
}


static void parse(ParseSink & sink, Source & source, const Path & path);



static void parseEntry(ParseSink & sink, Source & source, const Path & path)
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
            parse(sink, source, path + "/" + name);
        } else {
            throw badArchive("unknown field " + s);
            skipGeneric(source);
        }
    }
}


static void parseContents(ParseSink & sink, Source & source, const Path & path)
{
    unsigned long long size = readLongLong(source);
    
    sink.preallocateContents(size);

    unsigned long long left = size;
    unsigned char buf[65536];

    while (left) {
        checkInterrupt();
        unsigned int n = sizeof(buf);
        if ((unsigned long long) n > left) n = left;
        source(buf, n);
        sink.receiveContents(buf, n);
        left -= n;
    }

    readPadding(size, source);
}


static void parse(ParseSink & sink, Source & source, const Path & path)
{
    string s;

    s = readString(source);
    if (s != "(") throw badArchive("expected open tag");

    enum { tpUnknown, tpRegular, tpDirectory, tpSymlink } type = tpUnknown;

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
                sink.createRegularFile(path);
            }

            else if (t == "directory") {
                sink.createDirectory(path);
                type = tpDirectory;
            }

            else if (t == "symlink") {
                type = tpSymlink;
            }
            
            else throw badArchive("unknown file type " + t);
            
        }

        else if (s == "contents" && type == tpRegular) {
            parseContents(sink, source, path);
        }

        else if (s == "executable" && type == tpRegular) {
            readString(source);
            sink.isExecutable();
        }

        else if (s == "entry" && type == tpDirectory) {
            parseEntry(sink, source, path);
        }

        else if (s == "target" && type == tpSymlink) {
            string target = readString(source);
            sink.createSymlink(path, target);
        }

        else {
            throw badArchive("unknown field " + s);
            skipGeneric(source);
        }
    }
}


void parseDump(ParseSink & sink, Source & source)
{
    string version;    
    try {
        version = readString(source);
    } catch (SerialisationError & e) {
        /* This generally means the integer at the start couldn't be
           decoded.  Ignore and throw the exception below. */
    }
    if (version != archiveVersion1)
        throw badArchive("input doesn't look like a Nix archive");
    parse(sink, source, "");
}


struct RestoreSink : ParseSink
{
    Path dstPath;
    AutoCloseFD fd;

    void createDirectory(const Path & path)
    {
        Path p = dstPath + path;
        if (mkdir(p.c_str(), 0777) == -1)
            throw SysError(format("creating directory `%1%'") % p);
    };

    void createRegularFile(const Path & path)
    {
        Path p = dstPath + path;
        fd = open(p.c_str(), O_CREAT | O_EXCL | O_WRONLY, 0666);
        if (fd == -1) throw SysError(format("creating file `%1%'") % p);
    }

    void isExecutable()
    {
        struct stat st;
        if (fstat(fd, &st) == -1)
            throw SysError("fstat");
        if (fchmod(fd, st.st_mode | (S_IXUSR | S_IXGRP | S_IXOTH)) == -1)
            throw SysError("fchmod");
    }

    void preallocateContents(unsigned long long len)
    {
#if HAVE_POSIX_FALLOCATE
        if (len) {
            errno = posix_fallocate(fd, 0, len);
            if (errno) throw SysError(format("preallocating file of %1% bytes") % len);
        }
#endif
    }

    void receiveContents(unsigned char * data, unsigned int len)
    {
        writeFull(fd, data, len);
    }

    void createSymlink(const Path & path, const string & target)
    {
        Path p = dstPath + path;
        if (symlink(target.c_str(), p.c_str()) == -1)
            throw SysError(format("creating symlink `%1%'") % p);
    }
};

 
void restorePath(const Path & path, Source & source)
{
    RestoreSink sink;
    sink.dstPath = path;
    parseDump(sink, source);
}

 
}
