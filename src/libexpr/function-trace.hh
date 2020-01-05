#pragma once

#include "eval.hh"
#include <sys/time.h>

namespace nix {

struct FunctionCallTrace
{
    const Pos & pos;
    FunctionCallTrace(const Pos & pos);
    ~FunctionCallTrace();
};
}
