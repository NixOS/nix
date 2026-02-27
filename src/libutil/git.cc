#include <cerrno>
#include <algorithm>
#include <vector>
#include <map>
#include <regex>
#include <strings.h> // for strcasecmp

#include "nix/util/signals.hh"
#include "nix/util/configuration.hh"
#include "nix/util/hash.hh"

#include "nix/util/git.hh"
#include "nix/util/serialise.hh"

namespace nix::git {

using namespace nix;
using namespace std::string_literals;

std::optional<Mode> decodeMode(RawMode m)
{
    switch (m) {
    case (RawMode) Mode::Directory:
    case (RawMode) Mode::Executable:
    case (RawMode) Mode::Regular:
    case (RawMode) Mode::Symlink:
        return (Mode) m;
    default:
        return std::nullopt;
    }
}

static std::string getStringUntil(Source & source, char byte)
{
    std::string s;
    char n[1] = {0};
    source(std::string_view{n, 1});
    while (*n != byte) {
        s += *n;
        source(std::string_view{n, 1});
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

void parseBlob(
    FileSystemObjectSink & sink,
    const CanonPath & sinkPath,
    Source & source,
    BlobMode blobMode,
    const ExperimentalFeatureSettings & xpSettings)
{
    xpSettings.require(Xp::GitHashing);

    const unsigned long long size = std::stoi(getStringUntil(source, 0));

    auto doRegularFile = [&](bool executable) {
        sink.createRegularFile(sinkPath, [&](auto & crf) {
            if (executable)
                crf.isExecutable();

            crf.preallocateContents(size);

            unsigned long long left = size;
            std::string buf;
            buf.reserve(65536);

            while (left) {
                checkInterrupt();
                buf.resize(std::min((unsigned long long) buf.capacity(), left));
                source(buf);
                crf(buf);
                left -= buf.size();
            }
        });
    };

    switch (blobMode) {

    case BlobMode::Regular:
        doRegularFile(false);
        break;

    case BlobMode::Executable:
        doRegularFile(true);
        break;

    case BlobMode::Symlink: {
        std::string target;
        target.resize(size, '0');
        target.reserve(size);
        for (size_t n = 0; n < target.size();) {
            checkInterrupt();
            n += source.read(const_cast<char *>(target.c_str()) + n, target.size() - n);
        }

        sink.createSymlink(sinkPath, target);
        break;
    }

    default:
        assert(false);
    }
}

void parseTree(
    FileSystemObjectSink & sink,
    const CanonPath & sinkPath,
    Source & source,
    HashAlgorithm hashAlgo,
    fun<SinkHook> hook,
    const ExperimentalFeatureSettings & xpSettings)
{
    const unsigned long long size = std::stoi(getStringUntil(source, 0));
    unsigned long long left = size;

    sink.createDirectory(sinkPath);

    while (left) {
        std::string perms = getStringUntil(source, ' ');
        left -= perms.size();
        left -= 1;

        RawMode rawMode = std::stoi(perms, 0, 8);
        auto modeOpt = decodeMode(rawMode);
        if (!modeOpt)
            throw Error("Unknown Git permission: %o", rawMode);
        auto mode = std::move(*modeOpt);

        std::string name = getStringUntil(source, '\0');
        left -= name.size();
        left -= 1;

        const auto hashSize = regularHashSize(hashAlgo);
        std::string hashs = getString(source, hashSize);
        left -= hashSize;

        if (!(hashAlgo == HashAlgorithm::SHA1 || hashAlgo == HashAlgorithm::SHA256)) {
            throw Error("Unsupported hash algorithm for git trees: %s", printHashAlgo(hashAlgo));
        }

        Hash hash(hashAlgo);
        std::copy(hashs.begin(), hashs.end(), hash.hash);

        hook(
            CanonPath{name},
            TreeEntry{
                .mode = mode,
                .hash = hash,
            });
    }
}

ObjectType parseObjectType(Source & source, const ExperimentalFeatureSettings & xpSettings)
{
    xpSettings.require(Xp::GitHashing);

    auto type = getString(source, 5);

    if (type == "blob ") {
        return ObjectType::Blob;
    } else if (type == "tree ") {
        return ObjectType::Tree;
    } else
        throw Error("input doesn't look like a Git object");
}

void parse(
    FileSystemObjectSink & sink,
    const CanonPath & sinkPath,
    Source & source,
    BlobMode rootModeIfBlob,
    HashAlgorithm hashAlgo,
    fun<SinkHook> hook,
    const ExperimentalFeatureSettings & xpSettings)
{
    xpSettings.require(Xp::GitHashing);

    auto type = parseObjectType(source, xpSettings);

    switch (type) {
    case ObjectType::Blob:
        parseBlob(sink, sinkPath, source, rootModeIfBlob, xpSettings);
        break;
    case ObjectType::Tree:
        parseTree(sink, sinkPath, source, hashAlgo, hook, xpSettings);
        break;
    default:
        assert(false);
    };
}

std::optional<Mode> convertMode(SourceAccessor::Type type)
{
    switch (type) {
    case SourceAccessor::tSymlink:
        return Mode::Symlink;
    case SourceAccessor::tRegular:
        return Mode::Regular;
    case SourceAccessor::tDirectory:
        return Mode::Directory;
    case SourceAccessor::tChar:
    case SourceAccessor::tBlock:
    case SourceAccessor::tSocket:
    case SourceAccessor::tFifo:
        return std::nullopt;
    case SourceAccessor::tUnknown:
    default:
        unreachable();
    }
}

void restore(FileSystemObjectSink & sink, Source & source, HashAlgorithm hashAlgo, fun<RestoreHook> hook)
{
    parse(sink, CanonPath::root, source, BlobMode::Regular, hashAlgo, [&](CanonPath name, TreeEntry entry) {
        auto [accessor, from] = hook(entry.hash);
        auto stat = accessor->lstat(from);
        auto gotOpt = convertMode(stat.type);
        if (!gotOpt)
            throw Error(
                "file '%s' (git hash %s) has an unsupported type",
                from,
                entry.hash.to_string(HashFormat::Base16, false));
        auto & got = *gotOpt;
        if (got != entry.mode)
            throw Error(
                "git mode of file '%s' (git hash %s) is %o but expected %o",
                from,
                entry.hash.to_string(HashFormat::Base16, false),
                (RawMode) got,
                (RawMode) entry.mode);
        copyRecursive(*accessor, from, sink, name);
    });
}

void dumpBlobPrefix(uint64_t size, Sink & sink, const ExperimentalFeatureSettings & xpSettings)
{
    xpSettings.require(Xp::GitHashing);
    auto s = fmt("blob %d\0"s, std::to_string(size));
    sink(s);
}

void dumpTree(const Tree & entries, Sink & sink, const ExperimentalFeatureSettings & xpSettings)
{
    xpSettings.require(Xp::GitHashing);

    std::string v1;

    for (auto & [name, entry] : entries) {
        auto name2 = name;
        if (entry.mode == Mode::Directory) {
            assert(!name2.empty());
            assert(name2.back() == '/');
            name2.pop_back();
        }
        v1 += fmt("%o %s\0"s, static_cast<RawMode>(entry.mode), name2);
        std::copy(entry.hash.hash, entry.hash.hash + entry.hash.hashSize, std::back_inserter(v1));
    }

    {
        auto s = fmt("tree %d\0"s, v1.size());
        sink(s);
    }

    sink(v1);
}

Mode dump(
    const SourcePath & path,
    Sink & sink,
    fun<DumpHook> hook,
    PathFilter & filter,
    const ExperimentalFeatureSettings & xpSettings)
{
    auto st = path.lstat();

    switch (st.type) {
    case SourceAccessor::tRegular: {
        path.readFile(sink, [&](uint64_t size) { dumpBlobPrefix(size, sink, xpSettings); });
        return st.isExecutable ? Mode::Executable : Mode::Regular;
    }

    case SourceAccessor::tDirectory: {
        Tree entries;
        for (auto & [name, _] : path.readDirectory()) {
            auto child = path / name;
            if (!filter(child.path.abs()))
                continue;

            auto entry = hook(child);

            auto name2 = name;
            if (entry.mode == Mode::Directory)
                name2 += "/";

            entries.insert_or_assign(std::move(name2), std::move(entry));
        }
        dumpTree(entries, sink, xpSettings);
        return Mode::Directory;
    }

    case SourceAccessor::tSymlink: {
        auto target = path.readLink();
        dumpBlobPrefix(target.size(), sink, xpSettings);
        sink(target);
        return Mode::Symlink;
    }

    case SourceAccessor::tChar:
    case SourceAccessor::tBlock:
    case SourceAccessor::tSocket:
    case SourceAccessor::tFifo:
    case SourceAccessor::tUnknown:
    default:
        throw Error("file '%1%' has an unsupported type of %2%", path, st.typeString());
    }
}

TreeEntry dumpHash(HashAlgorithm ha, const SourcePath & path, PathFilter & filter)
{
    fun<DumpHook> hook = [&](const SourcePath & path) -> TreeEntry {
        auto hashSink = HashSink(ha);
        auto mode = dump(path, hashSink, hook, filter);
        auto hash = hashSink.finish().hash;
        return {
            .mode = mode,
            .hash = hash,
        };
    };

    return hook(path);
}

std::optional<LsRemoteRefLine> parseLsRemoteLine(std::string_view line)
{
    const static std::regex line_regex("^(ref: *)?([^\\s]+)(?:\\t+(.*))?$");
    std::match_results<std::string_view::const_iterator> match;
    if (!std::regex_match(line.cbegin(), line.cend(), match, line_regex))
        return std::nullopt;

    return LsRemoteRefLine{
        .kind = match[1].length() == 0 ? LsRemoteRefLine::Kind::Object : LsRemoteRefLine::Kind::Symbolic,
        .target = match[2],
        .reference = match[3].length() == 0 ? std::nullopt : std::optional<std::string>{match[3]}};
}

} // namespace nix::git
