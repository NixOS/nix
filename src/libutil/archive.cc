#include <cerrno>
#include <algorithm>
#include <vector>
#include <map>

#include <strings.h> // for strcasecmp

#include "archive.hh"
#include "config.hh"
#include "posix-source-accessor.hh"
#include "file-system.hh"
#include "signals.hh"

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
        sink << "contents";
        std::optional<uint64_t> size;
        readFile(path, sink, [&](uint64_t _size)
        {
            size = _size;
            sink << _size;
        });
        assert(size);
        writePadding(*size, sink);
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
                        debug("removing case hack suffix from '%s'", path / i.first);
                        name.erase(pos);
                    }
                    if (!unhacked.emplace(name, i.first).second)
                        throw Error("file name collision in between '%s' and '%s'",
                            (path / unhacked[name]),
                            (path / i.first));
                } else
                    unhacked.emplace(i.first, i.first);

            for (auto & i : unhacked)
                if (filter((path / i.first).abs())) {
                    sink << "entry" << "(" << "name" << i.first << "node";
                    dump(path / i.second);
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


time_t dumpPathAndGetMtime(const Path & path, Sink & sink, PathFilter & filter)
{
    auto [accessor, canonPath] = PosixSourceAccessor::createAtRoot(path);
    accessor.dumpPath(canonPath, sink, filter);
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


static void parseContents(CreateRegularFileSink & sink, Source & source)
{
    uint64_t size = readLongLong(source);

    sink.preallocateContents(size);

    uint64_t left = size;
    std::array<char, 65536> buf;

    while (left) {
        checkInterrupt();
        auto n = buf.size();
        if ((uint64_t)n > left) n = left;
        source(buf.data(), n);
        sink({buf.data(), n});
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


static void parse(FileSystemObjectSink & sink, Source & source, const Path & path)
{
    std::string s;

    s = readString(source);
    if (s != "(") throw badArchive("expected open tag");

    std::map<Path, int, CaseInsensitiveCompare> names;

    auto getString = [&]() {
        checkInterrupt();
        return readString(source);
    };

    // For first iteration
    s = getString();

    while (1) {

        if (s == ")") {
            break;
        }

        else if (s == "type") {
            std::string t = getString();

            if (t == "regular") {
                sink.createRegularFile(path, [&](auto & crf) {
                    while (1) {
                        s = getString();

                        if (s == "contents") {
                            parseContents(crf, source);
                        }

                        else if (s == "executable") {
                            auto s2 = getString();
                            if (s2 != "") throw badArchive("executable marker has non-empty value");
                            crf.isExecutable();
                        }

                        else break;
                    }
                });
            }

            else if (t == "directory") {
                sink.createDirectory(path);

                while (1) {
                    s = getString();

                    if (s == "entry") {
                        std::string name, prevName;

                        s = getString();
                        if (s != "(") throw badArchive("expected open tag");

                        while (1) {
                            s = getString();

                            if (s == ")") {
                                break;
                            } else if (s == "name") {
                                name = getString();
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

                    else break;
                }
            }

            else if (t == "symlink") {
                s = getString();

                if (s != "target")
                    throw badArchive("expected 'target' got " + s);

                std::string target = getString();
                sink.createSymlink(path, target);

                // for the next iteration
                s = getString();
            }

            else throw badArchive("unknown file type " + t);

        }

        else
            throw badArchive("unknown field " + s);
    }
}


void parseDump(FileSystemObjectSink & sink, Source & source)
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

    NullFileSystemObjectSink parseSink; /* just parse the NAR */

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
