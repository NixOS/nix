#include <cerrno>
#include <algorithm>
#include <vector>
#include <map>
#ifdef __MINGW32__
#include <iostream>
#endif

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
#ifndef __MINGW32__ // assume our Windows is fresh enough to suport posix semantics
    Setting<bool> useCaseHack{this,
        #if __APPLE__
            true,
        #else
            false,
        #endif
        "use-case-hack",
        "Whether to enable a Darwin-specific hack for dealing with file name collisions."};
#endif
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
#ifndef __MINGW32__
    AutoCloseFD fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (!fd) throw PosixError(format("opening file '%1%'") % path);
#else
std::cerr << "dumpContents(" << path << "," << size << ")" << std::endl;
    AutoCloseWindowsHandle fd = CreateFileW(pathW(path).c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (fd.get() == INVALID_HANDLE_VALUE)
        throw WinError("CreateFileW when dumpContents '%1%'", path);
#endif

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

#ifndef __MINGW32__
static void dump(const Path & path, Sink & sink, PathFilter & filter)
{
    checkInterrupt();

    struct stat st;
    if (lstat(path.c_str(), &st))
        throw PosixError(format("getting attributes of path '%1%'") % path);
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

#else

static void dump(const Path & path, const std::wstring & wpath, /* the same path, in UTF-8 and UTF-16 */
                 const DWORD * pdwFileAttributes, const uint64_t * pFileSize /* attributes might be known */,
                 Sink & sink, PathFilter & filter)
{
    checkInterrupt();

    DWORD dwFileAttributes;
    uint64_t filesize;
    if (pdwFileAttributes == NULL || pFileSize == NULL) {
        WIN32_FILE_ATTRIBUTE_DATA wfad;
        if (!GetFileAttributesExW(wpath.c_str(), GetFileExInfoStandard, &wfad))
            throw WinError("GetFileAttributesExW when canonicalisePathMetaData '%1%'", path);
        dwFileAttributes = wfad.dwFileAttributes;
        filesize = (uint64_t(wfad.nFileSizeHigh) << 32) + wfad.nFileSizeLow;
    } else {
        dwFileAttributes = *pdwFileAttributes;
        filesize = *pFileSize;
    }

    sink << "(";

    if ((dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0 && (dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
        sink << "type" << "regular";
//      if (???) sink << "executable" << "";
        dumpContents(path, filesize, sink);
    }

    else if ((dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0 && (dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        sink << "type" << "directory";

        std::map<std::wstring, std::pair<DWORD, uint64_t>> unhacked;

        WIN32_FIND_DATAW wfd;
        HANDLE hFind = FindFirstFileExW((wpath + L"\\*").c_str(), FindExInfoBasic, &wfd, FindExSearchNameMatch, NULL, 0);
        if (hFind == INVALID_HANDLE_VALUE) {
            throw WinError("FindFirstFileExW when dumping nar '%1%'", path);
        } else {
            do {
                if (wfd.cFileName[0] == '.' && wfd.cFileName[1] == '\0'
                 || wfd.cFileName[0] == '.' && wfd.cFileName[1] == '.' && wfd.cFileName[2] == '\0') {
                } else {
                    unhacked[wfd.cFileName] = std::make_pair(wfd.dwFileAttributes, (uint64_t(wfd.nFileSizeHigh) << 32) + wfd.nFileSizeLow);
                }
            } while(FindNextFileW(hFind, &wfd));
            WinError winError("FindNextFileW when dumping nar '%1%'", path);
            if (winError.lastError != ERROR_NO_MORE_FILES)
                throw winError;
            FindClose(hFind);
        }

        for (auto & i : unhacked)
            if (filter(path + "/" + to_bytes(i.first))) {
                sink << "entry" << "(" << "name" << to_bytes(i.first) << "node";
                dump(path + "/" + to_bytes(i.first), wpath + L'\\' + i.first, &i.second.first, &i.second.second, sink, filter);
                sink << ")";
            }
    }

    else if ((dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
        sink << "type" << "symlink" << "target" << readLink(path);
    }

    sink << ")";
}

#endif


void dumpPath(const Path & path, Sink & sink, PathFilter & filter)
{
    sink << narVersionMagic1;
    dump(path, pathW(path), NULL, NULL, sink, filter);
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
#ifndef __MINGW32__
                    if (archiveSettings.useCaseHack) {
                        auto i = names.find(name);
                        if (i != names.end()) {
                            debug(format("case collision between '%1%' and '%2%'") % i->first % name);
                            name += caseHackSuffix;
                            name += std::to_string(++i->second);
                        } else
                            names[name] = 0;
                    }
#endif
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
#ifndef __MINGW32__
    AutoCloseFD fd;
#else
    AutoCloseWindowsHandle fd;
#endif

    void createDirectory(const Path & path)
    {
        Path p = dstPath + path;
#ifndef __MINGW32__
        if (mkdir(p.c_str(), 0777) == -1)
            throw PosixError(format("creating directory '%1%'") % p);
#else
        if (!CreateDirectoryW(pathW(p).c_str(), NULL))
            throw WinError("CreateDirectoryW when RestoreSink '%1%'", p);
#endif
    };

    void createRegularFile(const Path & path)
    {
        Path p = dstPath + path;
#ifndef __MINGW32__
        fd = open(p.c_str(), O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC, 0666);
        if (!fd) throw PosixError(format("creating file '%1%'") % p);
#else
        fd = CreateFileW(pathW(p).c_str(), GENERIC_WRITE, 0, NULL, CREATE_NEW,
                         FILE_ATTRIBUTE_NORMAL | FILE_FLAG_POSIX_SEMANTICS, NULL);
        if (fd.get() == INVALID_HANDLE_VALUE)
            throw WinError("CreateFileW when RestoreSink '%1%'", p);
#endif
    }

#ifndef __MINGW32__
    void isExecutable()
    {
        struct stat st;
        if (fstat(fd.get(), &st) == -1)
            throw PosixError("fstat");

        if (fchmod(fd.get(), st.st_mode | (S_IXUSR | S_IXGRP | S_IXOTH)) == -1)
            throw PosixError("fchmod");
    }
#endif

    void preallocateContents(unsigned long long len)
    {
#if HAVE_POSIX_FALLOCATE
        if (len) {
            errno = posix_fallocate(fd.get(), 0, len);
            /* Note that EINVAL may indicate that the underlying
               filesystem doesn't support preallocation (e.g. on
               OpenSolaris).  Since preallocation is just an
               optimisation, ignore it. */
            if (errno && errno != EINVAL)
                throw PosixError(format("preallocating file of %1% bytes") % len);
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
