#pragma once
///@file

#include "types.hh"
#include "serialise.hh"


namespace nix {


/**
 * dumpPath creates a Nix archive of the specified path.
 *
 * @param path the file system data to dump. Dumping is recursive so if
 * this is a directory we dump it and all its children.
 *
 * @param [out] sink The serialised archive is fed into this sink.
 *
 * @param filter Can be used to skip certain files.
 *
 * The format is as follows:
 *
 * ```
 * IF path points to a REGULAR FILE:
 *   dump(path) = attrs(
 *     [ ("type", "regular")
 *     , ("contents", contents(path))
 *     ])
 *
 * IF path points to a DIRECTORY:
 *   dump(path) = attrs(
 *     [ ("type", "directory")
 *     , ("entries", concat(map(f, sort(entries(path)))))
 *     ])
 *     where f(fn) = attrs(
 *       [ ("name", fn)
 *       , ("file", dump(path + "/" + fn))
 *       ])
 *
 * where:
 *
 *   attrs(as) = concat(map(attr, as)) + encN(0)
 *   attrs((a, b)) = encS(a) + encS(b)
 *
 *   encS(s) = encN(len(s)) + s + (padding until next 64-bit boundary)
 *
 *   encN(n) = 64-bit little-endian encoding of n.
 *
 *   contents(path) = the contents of a regular file.
 *
 *   sort(strings) = lexicographic sort by 8-bit value (strcmp).
 *
 *   entries(path) = the entries of a directory, without `.` and
 *   `..`.
 *
 *   `+` denotes string concatenation.
 * ```
 */
void dumpPath(const Path & path, Sink & sink,
    PathFilter & filter = defaultPathFilter);

/**
 * Same as dumpPath(), but returns the last modified date of the path.
 */
time_t dumpPathAndGetMtime(const Path & path, Sink & sink,
    PathFilter & filter = defaultPathFilter);

/**
 * Dump an archive with a single file with these contents.
 *
 * @param s Contents of the file.
 */
void dumpString(std::string_view s, Sink & sink);

/**
 * \todo Fix this API, it sucks.
 */
struct ParseSink
{
    virtual void createDirectory(const Path & path) { };

    virtual void createRegularFile(const Path & path) { };
    virtual void closeRegularFile() { };
    virtual void isExecutable() { };
    virtual void preallocateContents(uint64_t size) { };
    virtual void receiveContents(std::string_view data) { };

    virtual void createSymlink(const Path & path, const std::string & target) { };
};

/**
 * If the NAR archive contains a single file at top-level, then save
 * the contents of the file to `s`.  Otherwise barf.
 */
struct RetrieveRegularNARSink : ParseSink
{
    bool regular = true;
    Sink & sink;

    RetrieveRegularNARSink(Sink & sink) : sink(sink) { }

    void createDirectory(const Path & path) override
    {
        regular = false;
    }

    void receiveContents(std::string_view data) override
    {
        sink(data);
    }

    void createSymlink(const Path & path, const std::string & target) override
    {
        regular = false;
    }
};

void parseDump(ParseSink & sink, Source & source);

void restorePath(const Path & path, Source & source);

/**
 * Read a NAR from 'source' and write it to 'sink'.
 */
void copyNAR(Source & source, Sink & sink);

void copyPath(const Path & from, const Path & to);


inline constexpr std::string_view narVersionMagic1 = "nix-archive-1";

inline constexpr std::string_view caseHackSuffix = "~nix~case~hack~";


}
