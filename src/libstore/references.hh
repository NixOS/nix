#pragma once

#include "types.hh"
#include "hash.hh"

namespace nix {

PathSet scanForReferences(const Path & path, const PathSet & refs,
    HashResult & hash);

void rewriteInPath(Path input, Path output, const StringRewrites & rewrites);

void rewriteReferences(StringSink & input, const StringRewrites & rewrites);

Hash hashModuloReferences(Path input, const std::set<std::string> & hashesToErase);
}
