#pragma once

#include "types.hh"
#include "serialise.hh"
#include "fs-sink.hh"

namespace nix {

enum struct GitMode {
    Directory,
    Executable,
    Regular,
};

void restoreGit(const Path & path, Source & source);

void parseGit(ParseSink & sink, Source & source);

// Dumps a single file to a sink
GitMode dumpGitBlob(const Path & path, const struct stat st, Sink & sink);

typedef std::map<string, std::pair<GitMode, Hash>> GitTree;

// Dumps a representation of a git tree to a sink
GitMode dumpGitTree(const GitTree & entries, Sink & sink);

// Recursively dumps path, hashing as we go
Hash dumpGitHash(
    std::function<std::unique_ptr<AbstractHashSink>()>,
    const Path & path,
    PathFilter & filter = defaultPathFilter);

// N.B. There is no way to recursively dump to a sink, as that doesn't make
// sense with the git hash/data model where the information is Merklized.
}
