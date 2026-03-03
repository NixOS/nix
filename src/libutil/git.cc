#include <cerrno>
#include <algorithm>
#include <charconv>
#include <vector>
#include <map>
#include <regex>
#include <strings.h> // for strcasecmp

#include "nix/util/signals.hh"
#include "nix/util/configuration.hh"
#include "nix/util/hash.hh"

#include "nix/util/git.hh"
#include "nix/util/serialise.hh"
#include "nix/util/util.hh"

namespace nix::git {

using namespace nix;
using namespace std::string_literals;

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

uint64_t parseBlob(Source & source, const ExperimentalFeatureSettings & xpSettings)
{
    xpSettings.require(Xp::GitHashing);

    auto sizeStr = getStringUntil(source, 0);
    uint64_t size;
    auto [ptr, ec] = std::from_chars(sizeStr.data(), sizeStr.data() + sizeStr.size(), size);
    if (ec != std::errc{})
        throw Error("invalid blob size '%s'", sizeStr);
    return size;
}

void parseTree(
    merkle::DirectorySink & sink,
    Source & source,
    HashAlgorithm hashAlgo,
    const ExperimentalFeatureSettings & xpSettings)
{
    xpSettings.require(Xp::GitHashing);

    auto sizeStr = getStringUntil(source, 0);
    uint64_t left;
    auto [ptr, ec] = std::from_chars(sizeStr.data(), sizeStr.data() + sizeStr.size(), left);
    if (ec != std::errc{})
        throw Error("invalid tree size '%s'", sizeStr);

    while (left) {
        std::string perms = getStringUntil(source, ' ');
        left -= perms.size();
        left -= 1;

        RawMode rawMode;
        auto [ptr, ec] = std::from_chars(perms.data(), perms.data() + perms.size(), rawMode, 8);
        if (ec != std::errc{})
            throw Error("invalid Git permission: %s", perms);
        auto modeOpt = decodeMode(rawMode);
        if (!modeOpt)
            throw Error("unknown Git permission: %o", rawMode);
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

        sink.insertChild(name, TreeEntry{.mode = mode, .hash = hash});
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
