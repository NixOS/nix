#pragma once
/**
 * @file
 *
 * Evaluation profiler interface definitions and builtin implementations.
 */

#include "nix/expr/nixexpr.hh"
#include "nix/util/ref.hh"

#include <vector>
#include <span>
#include <bitset>

namespace nix {

class EvalProfiler
{
public:
    enum Hook {
        preFunctionCall,
        postFunctionCall,
    };

    static constexpr std::size_t numHooks = Hook::postFunctionCall + 1;
    using Hooks = std::bitset<numHooks>;

private:
    std::optional<Hooks> neededHooks;

protected:
    /** Invalidate the cached neededHooks. */
    void invalidateNeededHooks()
    {
        neededHooks = std::nullopt;
    }

    /**
     * Get which hooks need to be called.
     *
     * This is the actual implementation which has to be defined by subclasses.
     * Public API goes through the needsHooks, which is a
     * non-virtual interface (NVI) which caches the return value.
     */
    virtual Hooks getNeededHooksImpl() const
    {
        return Hooks{};
    }

public:
    /**
     * Hook called in the EvalState::callFunction preamble.
     * Gets called only if (getNeededHooks().test(Hook::preFunctionCall)) is true.
     *
     * @param state Evaluator state.
     * @param v Function being invoked.
     * @param args Function arguments.
     * @param pos Function position.
     */
    virtual void
    preFunctionCallHook(const EvalState & state, const Value & v, std::span<Value *> args, const PosIdx pos)
    {
    }

    /**
     * Hook called on EvalState::callFunction exit.
     * Gets called only if (getNeededHooks().test(Hook::postFunctionCall)) is true.
     *
     * @param state Evaluator state.
     * @param v Function being invoked.
     * @param args Function arguments.
     * @param pos Function position.
     */
    virtual void
    postFunctionCallHook(const EvalState & state, const Value & v, std::span<Value *> args, const PosIdx pos)
    {
    }

    virtual ~EvalProfiler() = default;

    /**
     * Get which hooks need to be invoked for this EvalProfiler instance.
     */
    Hooks getNeededHooks()
    {
        if (neededHooks.has_value())
            return *neededHooks;
        return *(neededHooks = getNeededHooksImpl());
    }
};

/**
 * Profiler that invokes multiple profilers at once.
 */
class MultiEvalProfiler : public EvalProfiler
{
    std::vector<ref<EvalProfiler>> profilers;

    [[gnu::noinline]] Hooks getNeededHooksImpl() const override;

public:
    MultiEvalProfiler() = default;

    /** Register a profiler instance. */
    void addProfiler(ref<EvalProfiler> profiler);

    [[gnu::noinline]] void
    preFunctionCallHook(const EvalState & state, const Value & v, std::span<Value *> args, const PosIdx pos) override;
    [[gnu::noinline]] void
    postFunctionCallHook(const EvalState & state, const Value & v, std::span<Value *> args, const PosIdx pos) override;
};

class SampleStack : public EvalProfiler
{
    constexpr static std::chrono::duration sampleInterval = std::chrono::microseconds(10);

    Hooks getNeededHooksImpl() const override
    {
        return Hooks().set(preFunctionCall).set(postFunctionCall);
    }

public:
    struct LambdaFrameInfo
    {
        ExprLambda * expr;
        std::ostream & symbolize(const EvalState & state, std::ostream & os) const;
        auto operator<=>(const LambdaFrameInfo & rhs) const = default;
    };

    /** Primop call. */
    struct PrimOpFrameInfo
    {
        PrimOp * expr;
        std::ostream & symbolize(const EvalState & state, std::ostream & os) const;
        auto operator<=>(const PrimOpFrameInfo & rhs) const = default;
    };

    struct FallbackFrameInfo
    {
        PosIdx pos;
        std::ostream & symbolize(const EvalState & state, std::ostream & os) const;
        auto operator<=>(const FallbackFrameInfo & rhs) const = default;
    };

    using FrameInfo = std::variant<LambdaFrameInfo, PrimOpFrameInfo, FallbackFrameInfo>;
    using FrameStack = std::vector<FrameInfo>;

    [[gnu::noinline]] void
    preFunctionCallHook(const EvalState & state, const Value & v, std::span<Value *> args, const PosIdx pos) override;
    [[gnu::noinline]] void
    postFunctionCallHook(const EvalState & state, const Value & v, std::span<Value *> args, const PosIdx pos) override;

    std::ostream & saveProfile(const EvalState & state, std::ostream & os) const;

private:
    FrameStack stack;
    std::map<FrameStack, uint32_t> callCount;
    std::chrono::time_point<std::chrono::high_resolution_clock> lastStackSample =
        std::chrono::high_resolution_clock::now();
};

}
