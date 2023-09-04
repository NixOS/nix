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

void restoreGit(const Path & path, Source & source, const Path & realStoreDir, const Path & storeDir);

void parseGit(ParseSink & sink, Source & source, const Path & realStoreDir, const Path & storeDir);

// Dumps a single file to a sink
GitMode dumpGitBlob(const Path & path, const struct stat st, Sink & sink);

typedef std::map<std::string, std::pair<GitMode, Hash>> GitTree;

// Dumps a representation of a git tree to a sink
GitMode dumpGitTree(const GitTree & entries, Sink & sink);

// Recursively dumps path, hashing as we go
Hash dumpGitHash(HashType ht, const Path & path, PathFilter & filter = defaultPathFilter);

void dumpGit(HashType ht, const Path & path, Sink & sink, PathFilter & filter = defaultPathFilter);

}
