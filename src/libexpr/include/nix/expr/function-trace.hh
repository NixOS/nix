#pragma once
///@file

#include "nix/expr/eval.hh"
#include "nix/expr/eval-profiler.hh"

namespace nix {

class FunctionCallTrace : public EvalProfiler
{
    Hooks getNeededHooksImpl() const override
    {
        return Hooks().set(preFunctionCall).set(postFunctionCall);
    }

public:
    FunctionCallTrace() = default;

    [[gnu::noinline]] void
    preFunctionCallHook(EvalState & state, const Value & v, std::span<Value *> args, const PosIdx pos) override;
    [[gnu::noinline]] void
    postFunctionCallHook(EvalState & state, const Value & v, std::span<Value *> args, const PosIdx pos) override;
};

} // namespace nix
