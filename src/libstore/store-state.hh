#ifndef __STORESTATE_H
#define __STORESTATE_H

#include "derivations.hh"
#include "types.hh"
#include "db.hh"

namespace nix {

/* Create a state directory. */
void createStateDirsTxn(const Transaction & txn, const DerivationStateOutputDirs & stateOutputDirs, const DerivationStateOutputs & stateOutputs);

/* TODO */
Snapshots commitStatePathTxn(const Transaction & txn, const Path & statePath);

/* TODO */
//void updateRevisionsRecursivelyTxn(const Transaction & txn, const Path & statePath);

/* TODO */
//int readRevisionNumber(Path statePath);


void scanAndUpdateAllReferencesTxn(const Transaction & txn, const Path & statePath
								, PathSet & newFoundComponentReferences, PathSet & newFoundStateReferences);
								
void scanAndUpdateAllReferencesRecusivelyTxn(const Transaction & txn, const Path & statePath);

void revertToRevisionTxn(const Transaction & txn, Path & componentPath, Path & derivationPath, Path & statePath, int revision_arg, bool recursive);

}

#endif /* !__STORESTATE_H */
