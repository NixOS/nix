#ifndef __STORESTATE_H
#define __STORESTATE_H

#include "derivations.hh"

namespace nix {

/* Create a state directory. */
void createStateDirs(const DerivationStateOutputDirs & stateOutputDirs, const DerivationStateOutputs & stateOutputs, const StringPairs & env);

/* Create and prints the output prefixed with '[commandName]:' via print(lvlError,... of a shell command. */
void executeAndPrintShellCommand(const string & command, const string & commandName);

}

#endif /* !__STORESTATE_H */
