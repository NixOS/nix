#include "nix/expr/eval-profiler.hh"
#include "nix/expr/eval.hh"
#include "nix/util/environment-variables.hh"

#include <ostream>

namespace nix {

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

[[gnu::noinline]] void
SampleStack::preFunctionCallHook(const EvalState & state, const Value & v, std::span<Value *> args, const PosIdx pos)
{
    FrameInfo info;
    if (v.isLambda())
        info = LambdaFrameInfo{.expr = v.payload.lambda.fun};
    else if (v.isPrimOp())
        info = PrimOpFrameInfo{.expr = v.payload.primOp};
    else
        info = FallbackFrameInfo{.pos = pos};
    stack.push_back(info);
}

[[gnu::noinline]] void
SampleStack::postFunctionCallHook(const EvalState & state, const Value & v, std::span<Value *> args, const PosIdx pos)
{
    auto now = std::chrono::high_resolution_clock::now();

    if (now - lastStackSample > sampleInterval) {
        callCount[stack] += 1;
        lastStackSample = now;
    }

    if (!stack.empty())
        stack.pop_back();
}

std::ostream & SampleStack::LambdaFrameInfo::symbolize(const EvalState & state, std::ostream & os) const
{
    os << state.positions[expr->pos];
    if (expr->name)
        os << ":" << state.symbols[expr->name];
    return os;
}

std::ostream & SampleStack::FallbackFrameInfo::symbolize(const EvalState & state, std::ostream & os) const
{
    os << state.positions[pos];
    return os;
}

std::ostream & SampleStack::PrimOpFrameInfo::symbolize(const EvalState & state, std::ostream & os) const
{
    os << *expr;
    return os;
}

std::ostream & SampleStack::saveProfile(const EvalState & state, std::ostream & os) const
{
    for (auto & [stack, count] : callCount) {
        auto first = true;
        for (auto & pos : stack) {
            if (first)
                first = false;
            else
                os << ";";

            std::visit([&](auto && info) { info.symbolize(state, os); }, pos);
        }
        os << " " << count << std::endl;
    }

    return os;
}

}
