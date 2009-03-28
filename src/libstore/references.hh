#ifndef __REFERENCES_H
#define __REFERENCES_H

#include "types.hh"
#include "hash.hh"

namespace nix {

PathSet scanForReferences(const Path & path, const PathSet & refs,
    Hash & hash);
    
}

#endif /* !__REFERENCES_H */
