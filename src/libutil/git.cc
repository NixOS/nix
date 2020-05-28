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

static void parse(ParseSink & sink, Source & source, const Path & path);

// Converts a Path to a ParseSink
void restoreGit(const Path & path, Source & source) {

    RestoreSink sink;
    sink.dstPath = path;
    parseGit(sink, source);

}

void parseGit(ParseSink & sink, Source & source) {
    parse(sink, source, "");
}

static void parse(ParseSink & sink, Source & source, const Path & path) {
    uint8_t buf[4];

    std::basic_string_view<uint8_t> buf_v {
        (const uint8_t *) & buf,
        std::size(buf)
    };
    source(buf_v);
    if (buf_v.compare((const uint8_t *)"blob")) {
        uint8_t space;
        source(& space, 1);
    }
    else {
    }
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
        s1 += (format("%6d %s\0%s"s) % mode % i.first % i.second.second.hash).str();
    }

    std::string s2 = (format("tree %d\0%s"s) % s1.size() % s1).str();

    vector<uint8_t> v;
    std::copy(s2.begin(), s2.end(), std::back_inserter(v));
    sink(v.data(), v.size());
    return GitMode::Directory;
}

// Returns the perm in addition
std::pair<GitMode, Hash> dumpGitHashInternal(
    std::function<std::unique_ptr<AbstractHashSink>()> makeHashSink,
    const Path & path, PathFilter & filter)
{
    auto hashSink = makeHashSink();
    struct stat st;
    GitMode perm;
    if (lstat(path.c_str(), &st))
        throw SysError(format("getting attributes of path '%1%'") % path);

    if (S_ISREG(st.st_mode)) {
        perm = dumpGitBlob(path, st, *hashSink);
    } else if (S_ISDIR(st.st_mode)) {
        GitTree entries;
        for (auto & i : readDirectory(path))
            if (filter(path + "/" + i.name)) {
                entries[i.name] = dumpGitHashInternal(makeHashSink, path + "/" + i.name, filter);
            }
        perm = dumpGitTree(entries, *hashSink);
    } else {
        throw Error(format("file '%1%' has an unsupported type") % path);
    }

    auto hash = hashSink->finish().first;
    return std::pair { perm, hash };
}

Hash dumpGitHash(
    std::function<std::unique_ptr<AbstractHashSink>()> makeHashSink,
    const Path & path, PathFilter & filter)
{
    return dumpGitHashInternal(makeHashSink, path, filter).second;
}

}
