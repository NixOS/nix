#ifndef __REFERENCES_H
#define __REFERENCES_H

#include "types.hh"

namespace nix {

PathSet scanForReferences(const Path & path, const PathSet & refs);

PathSet scanForStateReferences(const string & path, const PathSet & statePaths);

PathSet scanForALLReferences(const string & path);
    
}

#endif /* !__REFERENCES_H */
