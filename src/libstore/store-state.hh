#ifndef __STORESTATE_H
#define __STORESTATE_H

#include "derivations.hh"

namespace nix {

/* Create a state directory. */
void createStateDirs(const DerivationStateOutputDirs & stateOutputDirs, const DerivationStateOutputs & stateOutputs);

}

#endif /* !__STORESTATE_H */
