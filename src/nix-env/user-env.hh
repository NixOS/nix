#ifndef __USER_ENV_H
#define __USER_ENV_H

#include "get-drvs.hh"

namespace nix {

DrvInfos queryInstalled(EvalState & state, const Path & userEnv);

}

#endif /* !__USER_ENV_H */




