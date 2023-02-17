#pragma once

#include "nix/expr/eval.hh"

#include <chrono>

namespace nix {

struct FunctionCallTrace
{
    const Pos pos;
    FunctionCallTrace(const Pos & pos);
    ~FunctionCallTrace();
};
}
