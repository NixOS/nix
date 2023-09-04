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
#include "serialise.hh"

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

static std::string getStringUntil(Source & source, char byte)
{
    std::string s;
    char n[1];
    source(std::string_view { n, 1 });
    while (*n != byte) {
        s += *n;
        source(std::string_view { n, 1 });
    }
    return s;
}

static std::string getString(Source & source, int n)
{
    std::string v;
    v.resize(n);
    source(v);
    return v;
}

// Unfortunately, no access to libstore headers here.
static std::string getStoreEntry(const Path & storeDir, Hash hash, std::string name)
{
    Hash hash1 = hashString(htSHA256, "fixed:out:git:" + hash.to_string(Base::Base16, true) + ":");
    Hash hash2 = hashString(htSHA256, "output:out:" + hash1.to_string(Base::Base16, true) + ":" + storeDir + ":" + name);
    Hash hash3 = compressHash(hash2, 20);

    return hash3.to_string(Base::Base32, false) + "-" + name;
}

static void parse(ParseSink & sink, Source & source, const Path & path, const Path & realStoreDir, const Path & storeDir)
{
    auto type = getString(source, 5);

    if (type == "blob ") {
        sink.createRegularFile(path);

        unsigned long long size = std::stoi(getStringUntil(source, 0));

        sink.preallocateContents(size);

        unsigned long long left = size;
        std::string buf;
        buf.reserve(65536);

        while (left) {
            checkInterrupt();
            buf.resize(std::min((unsigned long long)buf.capacity(), left));
            source(buf);
            sink.receiveContents(buf);
            left -= buf.size();
        }
    } else if (type == "tree ") {
        unsigned long long size = std::stoi(getStringUntil(source, 0));
        unsigned long long left = size;

        sink.createDirectory(path);

        while (left) {
            std::string perms = getStringUntil(source, ' ');
            left -= perms.size();
            left -= 1;

            int perm = std::stoi(perms);
            if (perm != 100644 && perm != 100755 && perm != 644 && perm != 755 && perm != 40000)
              throw Error("Unknown Git permission: %d", perm);

            std::string name = getStringUntil(source, 0);
            left -= name.size();
            left -= 1;

            std::string hashs = getString(source, 20);
            left -= 20;

            Hash hash(htSHA1);
            std::copy(hashs.begin(), hashs.end(), hash.hash);

            std::string entryName = getStoreEntry(storeDir, hash, "git");
            Path entry = absPath(realStoreDir + "/" + entryName);

            struct stat st;
            if (lstat(entry.c_str(), &st))
                throw SysError("getting attributes of path '%1%'", entry);

            if (S_ISREG(st.st_mode)) {
                if (perm == 40000)
                    throw SysError("file is a file but expected to be a directory '%1%'", entry);

                if (perm == 100755 || perm == 755)
                    sink.createExecutableFile(path + "/" + name);
                else
                    sink.createRegularFile(path + "/" + name);

                sink.copyFile(entry);
            } else if (S_ISDIR(st.st_mode)) {
                if (perm != 40000)
                    throw SysError("file is a directory but expected to be a file '%1%'", entry);

                sink.copyDirectory(realStoreDir + "/" + entryName, path + "/" + name);
            } else throw Error("file '%1%' has an unsupported type", entry);
        }
    } else throw Error("input doesn't look like a Git object");
}

// TODO stream file into sink, rather than reading into vector
GitMode dumpGitBlob(const Path & path, const struct stat st, Sink & sink)
{
    auto s = fmt("blob %d\0%s"s, std::to_string(st.st_size), readFile(path));

    std::string v;
    std::copy(s.begin(), s.end(), std::back_inserter(v));
    sink(v);
    return st.st_mode & S_IXUSR
        ? GitMode::Executable
        : GitMode::Regular;
}

GitMode dumpGitTree(const GitTree & entries, Sink & sink)
{
    std::vector<uint8_t> v1;

    for (auto & i : entries) {
        unsigned int mode;
        switch (i.second.first) {
        case GitMode::Directory: mode = 40000; break;
        case GitMode::Executable: mode = 100755; break;
        case GitMode::Regular: mode = 100644; break;
        }
        auto name = i.first;
        if (i.second.first == GitMode::Directory)
            name.pop_back();
        auto s1 = fmt("%d %s", mode, name);
        std::copy(s1.begin(), s1.end(), std::back_inserter(v1));
        v1.push_back(0);
        std::copy(i.second.second.hash, i.second.second.hash + 20, std::back_inserter(v1));
    }

    std::string v2;
    auto s2 = fmt("tree %d"s, v1.size());
    std::copy(s2.begin(), s2.end(), std::back_inserter(v2));
    v2.push_back(0);
    std::copy(v1.begin(), v1.end(), std::back_inserter(v2));

    sink(v2);

    return GitMode::Directory;
}

static std::pair<GitMode, Hash> dumpGitHashInternal(HashType ht, const Path & path, PathFilter & filter);

static GitMode dumpGitInternal(HashType ht, const Path & path, Sink & sink, PathFilter & filter)
{
    struct stat st;
    GitMode perm;
    if (lstat(path.c_str(), &st))
        throw SysError("getting attributes of path '%1%'", path);

    if (S_ISREG(st.st_mode))
        perm = dumpGitBlob(path, st, sink);
    else if (S_ISDIR(st.st_mode)) {
        GitTree entries;
        for (auto & i : readDirectory(path))
            if (filter(path + "/" + i.name)) {
                auto result = dumpGitHashInternal(ht, path + "/" + i.name, filter);

                // correctly observe git order, see
                // https://github.com/mirage/irmin/issues/352
                auto name = i.name;
                if (result.first == GitMode::Directory)
                    name += "/";

                entries.insert_or_assign(name, result);
            }
        perm = dumpGitTree(entries, sink);
    } else throw Error("file '%1%' has an unsupported type", path);

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
