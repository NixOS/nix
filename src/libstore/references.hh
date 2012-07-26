#pragma once

#include "types.hh"
#include "hash.hh"

namespace nix {

PathSet scanForReferences(const Path & path, const PathSet & refs,
    HashResult & hash);
    
}
