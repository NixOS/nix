#include "nix/expr/eval-profiler.hh"
#include "nix/expr/nixexpr.hh"

namespace nix {

void EvalProfiler::preFunctionCallHook(
    const EvalState & state, const Value & v, std::span<Value *> args, const PosIdx pos)
{
}

void EvalProfiler::postFunctionCallHook(
    const EvalState & state, const Value & v, std::span<Value *> args, const PosIdx pos)
{
}

void MultiEvalProfiler::preFunctionCallHook(
    const EvalState & state, const Value & v, std::span<Value *> args, const PosIdx pos)
{
    for (auto & profiler : profilers) {
        if (profiler->getNeededHooks().test(Hook::preFunctionCall))
            profiler->preFunctionCallHook(state, v, args, pos);
    }
}

void MultiEvalProfiler::postFunctionCallHook(
    const EvalState & state, const Value & v, std::span<Value *> args, const PosIdx pos)
{
    for (auto & profiler : profilers) {
        if (profiler->getNeededHooks().test(Hook::postFunctionCall))
            profiler->postFunctionCallHook(state, v, args, pos);
    }
}

EvalProfiler::Hooks MultiEvalProfiler::getNeededHooksImpl() const
{
    Hooks hooks;
    for (auto & p : profilers)
        hooks |= p->getNeededHooks();
    return hooks;
}

void MultiEvalProfiler::addProfiler(ref<EvalProfiler> profiler)
{
    profilers.push_back(profiler);
    invalidateNeededHooks();
}

}
