#include <cerrno>
#include <algorithm>
#include <vector>
#include <map>

#include <strings.h> // for strcasecmp

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>

#include "archive.hh"
#include "util.hh"
#include "config.hh"

namespace nix {

struct ArchiveSettings : Config
{
    Setting<bool> useCaseHack{this,
        #if __APPLE__
            true,
        #else
            false,
        #endif
        "use-case-hack",
        "Whether to enable a Darwin-specific hack for dealing with file name collisions."};
};

static ArchiveSettings archiveSettings;

static GlobalConfig::Register r1(&archiveSettings);

const std::string narVersionMagic1 = "nix-archive-1";

static string caseHackSuffix = "~nix~case~hack~";

PathFilter defaultPathFilter = [](const Path &) { return true; };


static void dumpContents(const Path & path, size_t size,
    Sink & sink)
{
    sink << "contents" << size;

    AutoCloseFD fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (!fd) throw SysError(format("opening file '%1%'") % path);

    std::vector<unsigned char> buf(65536);
    size_t left = size;

    while (left > 0) {
        auto n = std::min(left, buf.size());
        readFull(fd.get(), buf.data(), n);
        left -= n;
        sink(buf.data(), n);
    }

    writePadding(size, sink);
}


static void dump(const Path & path, Sink & sink, PathFilter & filter)
{
    checkInterrupt();

    struct stat st;
    if (lstat(path.c_str(), &st))
        throw SysError(format("getting attributes of path '%1%'") % path);

    sink << "(";

    if (S_ISREG(st.st_mode)) {
        sink << "type" << "regular";
        if (st.st_mode & S_IXUSR)
            sink << "executable" << "";
        dumpContents(path, (size_t) st.st_size, sink);
    }

    else if (S_ISDIR(st.st_mode)) {
        sink << "type" << "directory";

        /* If we're on a case-insensitive system like macOS, undo
           the case hack applied by restorePath(). */
        std::map<string, string> unhacked;
        for (auto & i : readDirectory(path))
            if (archiveSettings.useCaseHack) {
                string name(i.name);
                size_t pos = i.name.find(caseHackSuffix);
                if (pos != string::npos) {
                    debug(format("removing case hack suffix from '%1%'") % (path + "/" + i.name));
                    name.erase(pos);
                }
                if (unhacked.find(name) != unhacked.end())
                    throw Error(format("file name collision in between '%1%' and '%2%'")
                        % (path + "/" + unhacked[name]) % (path + "/" + i.name));
                unhacked[name] = i.name;
            } else
                unhacked[i.name] = i.name;

        for (auto & i : unhacked)
            if (filter(path + "/" + i.first)) {
                sink << "entry" << "(" << "name" << i.first << "node";
                dump(path + "/" + i.second, sink, filter);
                sink << ")";
            }
    }

    else if (S_ISLNK(st.st_mode))
        sink << "type" << "symlink" << "target" << readLink(path);

    else throw Error(format("file '%1%' has an unsupported type") % path);

    sink << ")";
}


void dumpPath(const Path & path, Sink & sink, PathFilter & filter)
{
    sink << narVersionMagic1;
    dump(path, sink, filter);
}


void dumpString(const std::string & s, Sink & sink)
{
    sink << narVersionMagic1 << "(" << "type" << "regular" << "contents" << s << ")";
}


static SerialisationError badArchive(string s)
{
    return SerialisationError("bad archive: " + s);
}


#if 0
static void skipGeneric(Source & source)
{
    if (readString(source) == "(") {
        while (readString(source) != ")")
            skipGeneric(source);
    }
}
#endif


static void parseContents(ParseSink & sink, Source & source, const Path & path)
{
    unsigned long long size = readLongLong(source);

    sink.preallocateContents(size);

    unsigned long long left = size;
    std::vector<unsigned char> buf(65536);

    while (left) {
        checkInterrupt();
        auto n = buf.size();
        if ((unsigned long long)n > left) n = left;
        source(buf.data(), n);
        sink.receiveContents(buf.data(), n);
        left -= n;
    }

    readPadding(size, source);
}


struct CaseInsensitiveCompare
{
    bool operator() (const string & a, const string & b) const
    {
        return strcasecmp(a.c_str(), b.c_str()) < 0;
    }
};


static void parse(ParseSink & sink, Source & source, const Path & path)
{
    string s;

    s = readString(source);
    if (s != "(") throw badArchive("expected open tag");

    enum { tpUnknown, tpRegular, tpDirectory, tpSymlink } type = tpUnknown;

    std::map<Path, int, CaseInsensitiveCompare> names;

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
            auto s = readString(source);
            if (s != "") throw badArchive("executable marker has non-empty value");
            sink.isExecutable();
        }

        else if (s == "entry" && type == tpDirectory) {
            string name, prevName;

            s = readString(source);
            if (s != "(") throw badArchive("expected open tag");

            while (1) {
                checkInterrupt();

                s = readString(source);

                if (s == ")") {
                    break;
                } else if (s == "name") {
                    name = readString(source);
                    if (name.empty() || name == "." || name == ".." || name.find('/') != string::npos || name.find((char) 0) != string::npos)
                        throw Error(format("NAR contains invalid file name '%1%'") % name);
                    if (name <= prevName)
                        throw Error("NAR directory is not sorted");
                    prevName = name;
                    if (archiveSettings.useCaseHack) {
                        auto i = names.find(name);
                        if (i != names.end()) {
                            debug(format("case collision between '%1%' and '%2%'") % i->first % name);
                            name += caseHackSuffix;
                            name += std::to_string(++i->second);
                        } else
                            names[name] = 0;
                    }
                } else if (s == "node") {
                    if (s.empty()) throw badArchive("entry name missing");
                    parse(sink, source, path + "/" + name);
                } else
                    throw badArchive("unknown field " + s);
            }
        }

        else if (s == "target" && type == tpSymlink) {
            string target = readString(source);
            sink.createSymlink(path, target);
        }

        else
            throw badArchive("unknown field " + s);
    }
}


void parseDump(ParseSink & sink, Source & source)
{
    string version;
    try {
        version = readString(source, narVersionMagic1.size());
    } catch (SerialisationError & e) {
        /* This generally means the integer at the start couldn't be
           decoded.  Ignore and throw the exception below. */
    }
    if (version != narVersionMagic1)
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
            throw SysError(format("creating directory '%1%'") % p);
    };

    void createRegularFile(const Path & path)
    {
        Path p = dstPath + path;
        fd = open(p.c_str(), O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC, 0666);
        if (!fd) throw SysError(format("creating file '%1%'") % p);
    }

    void isExecutable()
    {
        struct stat st;
        if (fstat(fd.get(), &st) == -1)
            throw SysError("fstat");
        if (fchmod(fd.get(), st.st_mode | (S_IXUSR | S_IXGRP | S_IXOTH)) == -1)
            throw SysError("fchmod");
    }

    void preallocateContents(unsigned long long len)
    {
#if HAVE_POSIX_FALLOCATE
        if (len) {
            errno = posix_fallocate(fd.get(), 0, len);
            /* Note that EINVAL may indicate that the underlying
               filesystem doesn't support preallocation (e.g. on
               OpenSolaris).  Since preallocation is just an
               optimisation, ignore it. */
            if (errno && errno != EINVAL && errno != EOPNOTSUPP && errno != ENOSYS)
                throw SysError(format("preallocating file of %1% bytes") % len);
        }
#endif
    }

    void receiveContents(unsigned char * data, unsigned int len)
    {
        writeFull(fd.get(), data, len);
    }

    void createSymlink(const Path & path, const string & target)
    {
        Path p = dstPath + path;
        nix::createSymlink(target, p);
    }
};


void restorePath(const Path & path, Source & source)
{
    RestoreSink sink;
    sink.dstPath = path;
    parseDump(sink, source);
}


void copyNAR(Source & source, Sink & sink)
{
    // FIXME: if 'source' is the output of dumpPath() followed by EOF,
    // we should just forward all data directly without parsing.

    ParseSink parseSink; /* null sink; just parse the NAR */

    LambdaSource wrapper([&](unsigned char * data, size_t len) {
        auto n = source.read(data, len);
        sink(data, n);
        return n;
    });

    parseDump(parseSink, wrapper);
}


}
