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
    Setting<bool> preallocateContents{this, false, "preallocate-contents",
        "Whether to preallocate files when writing objects with known size."};
};

static ArchiveSettings archiveSettings;

static GlobalConfig::Register rArchiveSettings(&archiveSettings);

const std::string narVersionMagic1 = "nix-archive-1";

static std::string caseHackSuffix = "~nix~case~hack~";

PathFilter defaultPathFilter = [](const Path &) { return true; };


static void dumpContents(const Path & path, off_t size,
    Sink & sink)
{
    sink << "contents" << size;

    AutoCloseFD fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (!fd) throw SysError("opening file '%1%'", path);

    std::vector<char> buf(65536);
    size_t left = size;

    while (left > 0) {
        auto n = std::min(left, buf.size());
        readFull(fd.get(), buf.data(), n);
        left -= n;
        sink({buf.data(), n});
    }

    writePadding(size, sink);
}


static time_t dump(const Path & path, Sink & sink, PathFilter & filter)
{
    checkInterrupt();

    auto st = lstat(path);
    time_t result = st.st_mtime;

    sink << "(";

    if (S_ISREG(st.st_mode)) {
        sink << "type" << "regular";
        if (st.st_mode & S_IXUSR)
            sink << "executable" << "";
        dumpContents(path, st.st_size, sink);
    }

    else if (S_ISDIR(st.st_mode)) {
        sink << "type" << "directory";

        /* If we're on a case-insensitive system like macOS, undo
           the case hack applied by restorePath(). */
        std::map<std::string, std::string> unhacked;
        for (auto & i : readDirectory(path))
            if (archiveSettings.useCaseHack) {
                std::string name(i.name);
                size_t pos = i.name.find(caseHackSuffix);
                if (pos != std::string::npos) {
                    debug(format("removing case hack suffix from '%1%'") % (path + "/" + i.name));
                    name.erase(pos);
                }
                if (!unhacked.emplace(name, i.name).second)
                    throw Error("file name collision in between '%1%' and '%2%'",
                       (path + "/" + unhacked[name]),
                       (path + "/" + i.name));
            } else
                unhacked.emplace(i.name, i.name);

        for (auto & i : unhacked)
            if (filter(path + "/" + i.first)) {
                sink << "entry" << "(" << "name" << i.first << "node";
                auto tmp_mtime = dump(path + "/" + i.second, sink, filter);
                if (tmp_mtime > result) {
                    result = tmp_mtime;
                }
                sink << ")";
            }
    }

    else if (S_ISLNK(st.st_mode))
        sink << "type" << "symlink" << "target" << readLink(path);

    else throw Error("file '%1%' has an unsupported type", path);

    sink << ")";

    return result;
}


time_t dumpPathAndGetMtime(const Path & path, Sink & sink, PathFilter & filter)
{
    sink << narVersionMagic1;
    return dump(path, sink, filter);
}

void dumpPath(const Path & path, Sink & sink, PathFilter & filter)
{
    dumpPathAndGetMtime(path, sink, filter);
}


void dumpString(std::string_view s, Sink & sink)
{
    sink << narVersionMagic1 << "(" << "type" << "regular" << "contents" << s << ")";
}


static SerialisationError badArchive(const std::string & s)
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
    uint64_t size = readLongLong(source);

    sink.preallocateContents(size);

    uint64_t left = size;
    std::vector<char> buf(65536);

    while (left) {
        checkInterrupt();
        auto n = buf.size();
        if ((uint64_t)n > left) n = left;
        source(buf.data(), n);
        sink.receiveContents({buf.data(), n});
        left -= n;
    }

    readPadding(size, source);
}


struct CaseInsensitiveCompare
{
    bool operator() (const std::string & a, const std::string & b) const
    {
        return strcasecmp(a.c_str(), b.c_str()) < 0;
    }
};


static void parse(ParseSink & sink, Source & source, const Path & path)
{
    std::string s;

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
            std::string t = readString(source);

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
            std::string name, prevName;

            s = readString(source);
            if (s != "(") throw badArchive("expected open tag");

            while (1) {
                checkInterrupt();

                s = readString(source);

                if (s == ")") {
                    break;
                } else if (s == "name") {
                    name = readString(source);
                    if (name.empty() || name == "." || name == ".." || name.find('/') != std::string::npos || name.find((char) 0) != std::string::npos)
                        throw Error("NAR contains invalid file name '%1%'", name);
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
                    if (name.empty()) throw badArchive("entry name missing");
                    parse(sink, source, path + "/" + name);
                } else
                    throw badArchive("unknown field " + s);
            }
        }

        else if (s == "target" && type == tpSymlink) {
            std::string target = readString(source);
            sink.createSymlink(path, target);
        }

        else
            throw badArchive("unknown field " + s);
    }
}


void parseDump(ParseSink & sink, Source & source)
{
    std::string version;
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

    void createDirectory(const Path & path) override
    {
        Path p = dstPath + path;
        if (mkdir(p.c_str(), 0777) == -1)
            throw SysError("creating directory '%1%'", p);
    };

    void createRegularFile(const Path & path) override
    {
        Path p = dstPath + path;
        fd = open(p.c_str(), O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC, 0666);
        if (!fd) throw SysError("creating file '%1%'", p);
    }

    void isExecutable() override
    {
        struct stat st;
        if (fstat(fd.get(), &st) == -1)
            throw SysError("fstat");
        if (fchmod(fd.get(), st.st_mode | (S_IXUSR | S_IXGRP | S_IXOTH)) == -1)
            throw SysError("fchmod");
    }

    void preallocateContents(uint64_t len) override
    {
        if (!archiveSettings.preallocateContents)
            return;

#if HAVE_POSIX_FALLOCATE
        if (len) {
            errno = posix_fallocate(fd.get(), 0, len);
            /* Note that EINVAL may indicate that the underlying
               filesystem doesn't support preallocation (e.g. on
               OpenSolaris).  Since preallocation is just an
               optimisation, ignore it. */
            if (errno && errno != EINVAL && errno != EOPNOTSUPP && errno != ENOSYS)
                throw SysError("preallocating file of %1% bytes", len);
        }
#endif
    }

    void receiveContents(std::string_view data) override
    {
        writeFull(fd.get(), data);
    }

    void createSymlink(const Path & path, const std::string & target) override
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

    TeeSource wrapper { source, sink };

    parseDump(parseSink, wrapper);
}


void copyPath(const Path & from, const Path & to)
{
    auto source = sinkToSource([&](Sink & sink) {
        dumpPath(from, sink);
    });
    restorePath(to, *source);
}


}
