#include <cerrno>
#include <algorithm>
#include <vector>
#include <map>

#include <strings.h> // for strcasecmp

#include "nix/util/archive.hh"
#include "nix/util/alignment.hh"
#include "nix/util/config-global.hh"
#include "nix/util/posix-source-accessor.hh"
#include "nix/util/source-path.hh"
#include "nix/util/file-system.hh"
#include "nix/util/signals.hh"

namespace nix {

struct ArchiveSettings : Config
{
    Setting<bool> useCaseHack{
        this,
#ifdef __APPLE__
        true,
#else
        false,
#endif
        "use-case-hack",
        "Whether to enable a macOS-specific hack for dealing with file name case collisions."};
};

static ArchiveSettings archiveSettings;

static GlobalConfig::Register rArchiveSettings(&archiveSettings);

PathFilter defaultPathFilter = [](const Path &) { return true; };

void SourceAccessor::dumpPath(const CanonPath & path, Sink & sink, PathFilter & filter)
{
    auto dumpContents = [&](const CanonPath & path) {
        sink << "contents";
        std::optional<uint64_t> size;
        readFile(path, sink, [&](uint64_t _size) {
            size = _size;
            sink << _size;
        });
        assert(size);
        writePadding(*size, sink);
    };

    sink << narVersionMagic1;

    [&, &this_(*this)](this const auto & dump, const CanonPath & path) -> void {
        checkInterrupt();

        auto st = this_.lstat(path);

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
            StringMap unhacked;
            for (auto & i : this_.readDirectory(path))
                if (archiveSettings.useCaseHack) {
                    std::string name(i.first);
                    size_t pos = i.first.find(caseHackSuffix);
                    if (pos != std::string::npos) {
                        debug("removing case hack suffix from '%s'", path / i.first);
                        name.erase(pos);
                    }
                    if (!unhacked.emplace(name, i.first).second)
                        throw Error(
                            "file name collision between '%s' and '%s'", (path / unhacked[name]), (path / i.first));
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
            sink << "type" << "symlink" << "target" << this_.readLink(path);

        else
            throw Error("file '%s' has an unsupported type", path);

        sink << ")";
    }(path);
}

time_t dumpPathAndGetMtime(const Path & path, Sink & sink, PathFilter & filter)
{
    auto path2 = PosixSourceAccessor::createAtRoot(path);
    path2.dumpPath(sink, filter);
    return path2.accessor.dynamic_pointer_cast<PosixSourceAccessor>()->mtime;
}

void dumpPath(const Path & path, Sink & sink, PathFilter & filter)
{
    dumpPathAndGetMtime(path, sink, filter);
}

void dumpString(std::string_view s, Sink & sink)
{
    sink << narVersionMagic1 << "(" << "type" << "regular" << "contents" << s << ")";
}

template<typename... Args>
static SerialisationError badArchive(std::string_view s, const Args &... args)
{
    return SerialisationError("bad archive: " + s, args...);
}

static void parseContents(CreateRegularFileSink & sink, Source & source)
{
    uint64_t size = readLongLong(source);

    sink.preallocateContents(size);

    if (sink.skipContents) {
        source.skip(alignUp(size, 8));
        return;
    }

    uint64_t left = size;
    std::array<char, 65536> buf;

    while (left) {
        checkInterrupt();
        auto n = buf.size();
        if ((uint64_t) n > left)
            n = left;
        source(buf.data(), n);
        sink({buf.data(), n});
        left -= n;
    }

    readPadding(size, source);
}

struct CaseInsensitiveCompare
{
    bool operator()(const std::string & a, const std::string & b) const
    {
        return strcasecmp(a.c_str(), b.c_str()) < 0;
    }
};

static void parse(FileSystemObjectSink & sink, Source & source, const CanonPath & path)
{
    auto getString = [&]() {
        checkInterrupt();
        return readString(source);
    };

    auto expectTag = [&](std::string_view expected) {
        auto tag = getString();
        if (tag != expected)
            throw badArchive("expected tag '%s', got '%s'", expected, tag.substr(0, 1024));
    };

    expectTag("(");

    expectTag("type");

    auto type = getString();

    if (type == "regular") {
        sink.createRegularFile(path, [&](auto & crf) {
            auto tag = getString();

            if (tag == "executable") {
                auto s2 = getString();
                if (s2 != "")
                    throw badArchive("executable marker has non-empty value");
                crf.isExecutable();
                tag = getString();
            }

            if (tag != "contents")
                throw badArchive("expected tag 'contents', got '%s'", tag);

            parseContents(crf, source);

            expectTag(")");
        });
    }

    else if (type == "directory") {
        sink.createDirectory(path);

        std::map<Path, int, CaseInsensitiveCompare> names;

        std::string prevName;

        while (1) {
            auto tag = getString();

            if (tag == ")")
                break;

            if (tag != "entry")
                throw badArchive("expected tag 'entry' or ')', got '%s'", tag);

            expectTag("(");

            expectTag("name");

            auto name = getString();
            if (name.empty() || name == "." || name == ".." || name.find('/') != std::string::npos
                || name.find((char) 0) != std::string::npos)
                throw badArchive("NAR contains invalid file name '%1%'", name);
            if (name <= prevName)
                throw badArchive("NAR directory is not sorted");
            prevName = name;
            if (archiveSettings.useCaseHack) {
                auto i = names.find(name);
                if (i != names.end()) {
                    debug("case collision between '%1%' and '%2%'", i->first, name);
                    name += caseHackSuffix;
                    name += std::to_string(++i->second);
                    auto j = names.find(name);
                    if (j != names.end())
                        throw badArchive(
                            "NAR contains file name '%s' that collides with case-hacked file name '%s'",
                            prevName,
                            j->first);
                } else
                    names[name] = 0;
            }

            expectTag("node");

            parse(sink, source, path / name);

            expectTag(")");
        }
    }

    else if (type == "symlink") {
        expectTag("target");

        auto target = getString();
        sink.createSymlink(path, target);

        expectTag(")");
    }

    else
        throw badArchive("unknown file type '%s'", type);
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
    parse(sink, source, CanonPath::root);
}

void restorePath(const std::filesystem::path & path, Source & source, bool startFsync)
{
    RestoreSink sink{startFsync};
    sink.dstPath = path;
    parseDump(sink, source);
}

void copyNAR(Source & source, Sink & sink)
{
    // FIXME: if 'source' is the output of dumpPath() followed by EOF,
    // we should just forward all data directly without parsing.

    NullFileSystemObjectSink parseSink; /* just parse the NAR */

    TeeSource wrapper{source, sink};

    parseDump(parseSink, wrapper);
}

} // namespace nix
