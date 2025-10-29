#pragma once
/**
 * @file
 *
 * Evaluation profiler interface definitions and builtin implementations.
 */

#include "nix/util/ref.hh"

#include <vector>
#include <span>
#include <bitset>
#include <optional>
#include <filesystem>

namespace nix {

class EvalState;
class PosIdx;
struct Value;

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
    virtual void preFunctionCallHook(EvalState & state, const Value & v, std::span<Value *> args, const PosIdx pos);

    /**
     * Hook called on EvalState::callFunction exit.
     * Gets called only if (getNeededHooks().test(Hook::postFunctionCall)) is true.
     *
     * @param state Evaluator state.
     * @param v Function being invoked.
     * @param args Function arguments.
     * @param pos Function position.
     */
    virtual void postFunctionCallHook(EvalState & state, const Value & v, std::span<Value *> args, const PosIdx pos);

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
    preFunctionCallHook(EvalState & state, const Value & v, std::span<Value *> args, const PosIdx pos) override;
    [[gnu::noinline]] void
    postFunctionCallHook(EvalState & state, const Value & v, std::span<Value *> args, const PosIdx pos) override;
};

ref<EvalProfiler> makeSampleStackProfiler(EvalState & state, std::filesystem::path profileFile, uint64_t frequency);

} // namespace nix
