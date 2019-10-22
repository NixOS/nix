#pragma once

#include "eval.hh"
#include <sys/time.h>

namespace nix {

struct FunctionCallTrace
{
    const Pos & pos;

    FunctionCallTrace(const Pos & pos) : pos(pos) {
        auto duration = std::chrono::high_resolution_clock::now().time_since_epoch();
        auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(duration);
        printMsg(lvlInfo, "function-trace entered %1% at %2%", pos, ns.count());
    }

    ~FunctionCallTrace() {
        auto duration = std::chrono::high_resolution_clock::now().time_since_epoch();
        auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(duration);
        printMsg(lvlInfo, "function-trace exited %1% at %2%", pos, ns.count());
    }
};
}
