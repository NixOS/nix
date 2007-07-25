#ifndef __STORESTATE_H
#define __STORESTATE_H

#include "derivations.hh"
#include "types.hh"
#include "db.hh"

namespace nix {

/* Create a state directory. */
void createStateDirs(const DerivationStateOutputDirs & stateOutputDirs, const DerivationStateOutputs & stateOutputs);

/* TODO */
Snapshots commitStatePathTxn(const Transaction & txn, const Path & statePath);

/* TODO */
//void updateRevisionsRecursivelyTxn(const Transaction & txn, const Path & statePath);

/* TODO */
//int readRevisionNumber(Path statePath);

}

#endif /* !__STORESTATE_H */
