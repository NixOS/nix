#include "build-result.hh"

namespace nix {

GENERATE_CMP_EXT(
    ,
    BuildResult,
    me->status,
    me->errorMsg,
    me->timesBuilt,
    me->isNonDeterministic,
    me->builtOutputs,
    me->startTime,
    me->stopTime,
    me->cpuUser,
    me->cpuSystem);

}
