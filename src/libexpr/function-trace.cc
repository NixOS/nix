#include "nix/expr/function-trace.hh"
#include "nix/util/logging.hh"

namespace nix {

void FunctionCallTrace::preFunctionCallHook(
    EvalState & state, const Value & v, std::span<Value *> args, const PosIdx pos)
{
    auto duration = std::chrono::high_resolution_clock::now().time_since_epoch();
    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(duration);
    printMsg(lvlInfo, "function-trace entered %1% at %2%", state.positions[pos], ns.count());
}

void FunctionCallTrace::postFunctionCallHook(
    EvalState & state, const Value & v, std::span<Value *> args, const PosIdx pos)
{
    auto duration = std::chrono::high_resolution_clock::now().time_since_epoch();
    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(duration);
    printMsg(lvlInfo, "function-trace exited %1% at %2%", state.positions[pos], ns.count());
}

} // namespace nix
