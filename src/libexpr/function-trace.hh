#pragma once

#include "eval.hh"

#include <chrono>

namespace nix {

struct FunctionCallTrace
{
    const Pos & pos;
    FunctionCallTrace(const Pos & pos);
    ~FunctionCallTrace();
};
}
