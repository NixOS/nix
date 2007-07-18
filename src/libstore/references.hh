#ifndef __REFERENCES_H
#define __REFERENCES_H

#include "types.hh"
#include "db.hh"

namespace nix {

/* Scans for Component References (currently doesnt add solid dependencys) */
PathSet scanForReferences(const Path & path, const PathSet & refs);
PathSet scanForReferencesTxn(const Transaction & txn, const Path & path, const PathSet & refs);

/* Scans for State References and adds solid state dependencys*/
PathSet scanForStateReferences(const Path & path, const PathSet & refs);
PathSet scanForStateReferencesTxn(const Transaction & txn, const Path & path, const PathSet & refs);

/* The original scanForReferences */
PathSet scanForReferencesTxn_(const Transaction & txn, const Path & path, const PathSet & refs);

}

#endif /* !__REFERENCES_H */
