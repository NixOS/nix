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

#include "util.hh"
#include "config.hh"
#include "hash.hh"

#include "git.hh"

using namespace std::string_literals;

namespace nix {

static void parse(ParseSink & sink, Source & source, const Path & path, const Path & realStoreDir, const Path & storeDir);

// Converts a Path to a ParseSink
void restoreGit(const Path & path, Source & source, const Path & realStoreDir, const Path & storeDir) {
    RestoreSink sink;
    sink.dstPath = path;
    parseGit(sink, source, realStoreDir, storeDir);
}

void parseGit(ParseSink & sink, Source & source, const Path & realStoreDir, const Path & storeDir)
{
    parse(sink, source, "", realStoreDir, storeDir);
}

static string getStringUntil(Source & source, char byte)
{
    string s;
    unsigned char n[1];
    source(n, 1);
    while (*n != byte) {
        s += *n;
        source(n, 1);
    }
    return s;
}

static string getString(Source & source, int n)
{
    std::vector<unsigned char> v(n);
    source(v.data(), n);
    return std::string(v.begin(), v.end());
}

// Unfortunately, no access to libstore headers here.
static Path getStorePath(const Path & realStoreDir, const Path & storeDir, Hash hash, string name)
{
    Hash hash1 = hashString(htSHA256, "fixed:out:git:" + hash.to_string(Base16) + ":");
    Hash hash2 = hashString(htSHA256, "output:out:" + hash1.to_string(Base16) + ":" + storeDir + ":" + name);
    Hash hash3 = compressHash(hash2, 20);

    return realStoreDir + "/" + hash3.to_string(Base32, false) + "-" + name;
}

static void parse(ParseSink & sink, Source & source, const Path & path, const Path & realStoreDir, const Path & storeDir)
{
    auto type = getString(source, 5);

    if (type == "blob ") {
        sink.createRegularFile(path);

        unsigned long long size = std::stoi(getStringUntil(source, 0));

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
    } else if (type == "tree ") {
        unsigned long long size = std::stoi(getStringUntil(source, 0));
        unsigned long long left = size;

        sink.createDirectory(path);

        while (left) {
            string perms = getStringUntil(source, ' ');
            left -= perms.size();
            left -= 1;

            int perm = std::stoi(perms);
            if (perm != 100644 && perm != 100755 && perm != 644 && perm != 755 && perm != 40000)
              throw Error(format("Unknown Git permission: %d") % perm);

            string name = getStringUntil(source, 0);
            left -= name.size();
            left -= 1;

            string hashs = getString(source, 20);
            left -= 20;

            Hash hash(htSHA1);
            std::copy(hashs.begin(), hashs.end(), hash.hash);

            Path entry = getStorePath(realStoreDir, storeDir, hash, name);

            struct stat st;
            if (lstat(entry.c_str(), &st))
                throw SysError(format("getting attributes of path '%1%'") % entry);

            if (S_ISREG(st.st_mode)) {
                if (perm == 40000)
                    throw SysError(format("file is a file but expected to be a directory '%1%'") % entry);

                if (perm == 100755 || perm == 755)
                    sink.createExecutableFile(path + "/" + name);
                else
                    sink.createRegularFile(path + "/" + name);

                unsigned long long size = st.st_size;
                sink.preallocateContents(size);

                unsigned long long left = size;
                std::vector<unsigned char> buf(65536);

                StringSink ssink;
                readFile(entry, ssink);
                AutoCloseFD fd = open(entry.c_str(), O_RDONLY | O_CLOEXEC);

                while (left) {
                    checkInterrupt();
                    auto n = buf.size();
                    if ((unsigned long long)n > left) n = left;
                    ssink(buf.data(), n);
                    sink.receiveContents(buf.data(), n);
                    left -= n;
                }
            } else if (S_ISDIR(st.st_mode)) {
                if (perm != 40000)
                    throw SysError(format("file is a directory but expected to be a file '%1%'") % entry);

                sink.createSymlink(path + "/" + name, entry);
            } else throw Error(format("file '%1%' has an unsupported type") % entry);
        }
    } else throw Error("input doesn't look like a Git object");
}

// TODO stream file into sink, rather than reading into vector
GitMode dumpGitBlob(const Path & path, const struct stat st, Sink & sink)
{
    auto s = (format("blob %d\0%s"s) % std::to_string(st.st_size) % readFile(path)).str();

    vector<uint8_t> v;
    std::copy(s.begin(), s.end(), std::back_inserter(v));
    sink(v.data(), v.size());
    return st.st_mode & S_IXUSR
        ? GitMode::Executable
        : GitMode::Regular;
}

GitMode dumpGitTree(const GitTree & entries, Sink & sink)
{
    std::string s1 = "";
    for (auto & i : entries) {
        unsigned int mode;
        switch (i.second.first) {
        case GitMode::Directory: mode = 40000; break;
        case GitMode::Executable: mode = 100755; break;
        case GitMode::Regular: mode = 100644; break;
        }
        s1 += (format("%06d %s\0%s"s) % mode % i.first % i.second.second.hash).str();
    }

    std::string s2 = (format("tree %d\0%s"s) % s1.size() % s1).str();

    vector<uint8_t> v;
    std::copy(s2.begin(), s2.end(), std::back_inserter(v));
    sink(v.data(), v.size());
    return GitMode::Directory;
}

static std::pair<GitMode, Hash> dumpGitHashInternal(HashType ht, const Path & path, PathFilter & filter);

static GitMode dumpGitInternal(HashType ht, const Path & path, Sink & sink, PathFilter & filter)
{
    struct stat st;
    GitMode perm;
    if (lstat(path.c_str(), &st))
        throw SysError(format("getting attributes of path '%1%'") % path);

    if (S_ISREG(st.st_mode))
        perm = dumpGitBlob(path, st, sink);
    else if (S_ISDIR(st.st_mode)) {
        GitTree entries;
        for (auto & i : readDirectory(path))
            if (filter(path + "/" + i.name))
                entries[i.name] = dumpGitHashInternal(ht, path + "/" + i.name, filter);
        perm = dumpGitTree(entries, sink);
    } else throw Error(format("file '%1%' has an unsupported type") % path);

    return perm;
}


static std::pair<GitMode, Hash> dumpGitHashInternal(HashType ht, const Path & path, PathFilter & filter)
{
    auto hashSink = new HashSink(ht);
    auto perm = dumpGitInternal(ht, path, *hashSink, filter);
    auto hash = hashSink->finish().first;
    return std::pair { perm, hash };
}

Hash dumpGitHash(HashType ht, const Path & path, PathFilter & filter)
{
    return dumpGitHashInternal(ht, path, filter).second;
}

void dumpGit(HashType ht, const Path & path, Sink & sink, PathFilter & filter)
{
    dumpGitInternal(ht, path, sink, filter);
}

}
