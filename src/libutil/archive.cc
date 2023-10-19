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
#include "source-accessor.hh"

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

static GlobalConfig::Register rArchiveSettings(&archiveSettings);

PathFilter defaultPathFilter = [](const Path &) { return true; };


void SourceAccessor::dumpPath(
    const CanonPath & path,
    Sink & sink,
    PathFilter & filter)
{
    auto dumpContents = [&](const CanonPath & path)
    {
        /* It would be nice if this was streaming, but we need the
           size before the contents. */
        auto s = readFile(path);
        sink << "contents" << s.size();
        sink(s);
        writePadding(s.size(), sink);
    };

    std::function<void(const CanonPath & path)> dump;

    dump = [&](const CanonPath & path) {
        checkInterrupt();

        auto st = lstat(path);

        sink << "(";

        if (st.type == tRegular) {
            sink << "type" << "regular";
            if (st.isExecutable)
                sink << "executable" << "";
            dumpContents(path);
        }

        else if (st.type == tDirectory) {
            sink << "type" << "directory";

            /* If we're on a case-insensitive system like macOS, undo
               the case hack applied by restorePath(). */
            std::map<std::string, std::string> unhacked;
            for (auto & i : readDirectory(path))
                if (archiveSettings.useCaseHack) {
                    std::string name(i.first);
                    size_t pos = i.first.find(caseHackSuffix);
                    if (pos != std::string::npos) {
                        debug("removing case hack suffix from '%s'", path + i.first);
                        name.erase(pos);
                    }
                    if (!unhacked.emplace(name, i.first).second)
                        throw Error("file name collision in between '%s' and '%s'",
                            (path + unhacked[name]),
                            (path + i.first));
                } else
                    unhacked.emplace(i.first, i.first);

            for (auto & i : unhacked)
                if (filter((path + i.first).abs())) {
                    sink << "entry" << "(" << "name" << i.first << "node";
                    dump(path + i.second);
                    sink << ")";
                }
        }

        else if (st.type == tSymlink)
            sink << "type" << "symlink" << "target" << readLink(path);

        else throw Error("file '%s' has an unsupported type", path);

        sink << ")";
    };

    sink << narVersionMagic1;
    dump(path);
}


struct FSSourceAccessor : SourceAccessor
{
    time_t mtime = 0; // most recent mtime seen

    std::string readFile(const CanonPath & path) override
    {
        return nix::readFile(path.abs());
    }

    bool pathExists(const CanonPath & path) override
    {
        return nix::pathExists(path.abs());
    }

    Stat lstat(const CanonPath & path) override
    {
        auto st = nix::lstat(path.abs());
        mtime = std::max(mtime, st.st_mtime);
        return Stat {
            .type =
                S_ISREG(st.st_mode) ? tRegular :
                S_ISDIR(st.st_mode) ? tDirectory :
                S_ISLNK(st.st_mode) ? tSymlink :
                tMisc,
            .isExecutable = S_ISREG(st.st_mode) && st.st_mode & S_IXUSR
        };
    }

    DirEntries readDirectory(const CanonPath & path) override
    {
        DirEntries res;
        for (auto & entry : nix::readDirectory(path.abs())) {
            std::optional<Type> type;
            switch (entry.type) {
            case DT_REG: type = Type::tRegular; break;
            case DT_LNK: type = Type::tSymlink; break;
            case DT_DIR: type = Type::tDirectory; break;
            }
            res.emplace(entry.name, type);
        }
        return res;
    }

    std::string readLink(const CanonPath & path) override
    {
        return nix::readLink(path.abs());
    }
};


time_t dumpPathAndGetMtime(const Path & path, Sink & sink, PathFilter & filter)
{
    FSSourceAccessor accessor;
    accessor.dumpPath(CanonPath::fromCwd(path), sink, filter);
    return accessor.mtime;
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
            sink.closeRegularFile();
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
                            debug("case collision between '%1%' and '%2%'", i->first, name);
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
