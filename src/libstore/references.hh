#ifndef __REFERENCES_H
#define __REFERENCES_H

#include "types.hh"

namespace nix {

PathSet scanForReferences(const Path & path, const PathSet & refs);
    
}

#endif /* !__REFERENCES_H */
