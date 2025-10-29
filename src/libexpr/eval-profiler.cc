#include "nix/expr/eval-profiler.hh"
#include "nix/expr/nixexpr.hh"
#include "nix/expr/eval.hh"
#include "nix/util/lru-cache.hh"

namespace nix {

void EvalProfiler::preFunctionCallHook(EvalState & state, const Value & v, std::span<Value *> args, const PosIdx pos) {}

void EvalProfiler::postFunctionCallHook(EvalState & state, const Value & v, std::span<Value *> args, const PosIdx pos)
{
}

void MultiEvalProfiler::preFunctionCallHook(
    EvalState & state, const Value & v, std::span<Value *> args, const PosIdx pos)
{
    for (auto & profiler : profilers) {
        if (profiler->getNeededHooks().test(Hook::preFunctionCall))
            profiler->preFunctionCallHook(state, v, args, pos);
    }
}

void MultiEvalProfiler::postFunctionCallHook(
    EvalState & state, const Value & v, std::span<Value *> args, const PosIdx pos)
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

namespace {

class PosCache : private LRUCache<PosIdx, Pos>
{
    const EvalState & state;

public:
    PosCache(const EvalState & state)
        : LRUCache(524288) /* ~40MiB */
        , state(state)
    {
    }

    Pos lookup(PosIdx posIdx)
    {
        auto posOrNone = LRUCache::get(posIdx);
        if (posOrNone)
            return *posOrNone;

        auto pos = state.positions[posIdx];
        upsert(posIdx, pos);
        return pos;
    }
};

struct LambdaFrameInfo
{
    ExprLambda * expr;
    /** Position where the lambda has been called. */
    PosIdx callPos = noPos;
    std::ostream & symbolize(const EvalState & state, std::ostream & os, PosCache & posCache) const;
    auto operator<=>(const LambdaFrameInfo & rhs) const = default;
};

/** Primop call. */
struct PrimOpFrameInfo
{
    const PrimOp * expr;
    /** Position where the primop has been called. */
    PosIdx callPos = noPos;
    std::ostream & symbolize(const EvalState & state, std::ostream & os, PosCache & posCache) const;
    auto operator<=>(const PrimOpFrameInfo & rhs) const = default;
};

/** Used for functor calls (attrset with __functor attr). */
struct FunctorFrameInfo
{
    PosIdx pos;
    std::ostream & symbolize(const EvalState & state, std::ostream & os, PosCache & posCache) const;
    auto operator<=>(const FunctorFrameInfo & rhs) const = default;
};

struct DerivationStrictFrameInfo
{
    PosIdx callPos = noPos;
    std::string drvName;
    std::ostream & symbolize(const EvalState & state, std::ostream & os, PosCache & posCache) const;
    auto operator<=>(const DerivationStrictFrameInfo & rhs) const = default;
};

/** Fallback frame info. */
struct GenericFrameInfo
{
    PosIdx pos;
    std::ostream & symbolize(const EvalState & state, std::ostream & os, PosCache & posCache) const;
    auto operator<=>(const GenericFrameInfo & rhs) const = default;
};

using FrameInfo =
    std::variant<LambdaFrameInfo, PrimOpFrameInfo, FunctorFrameInfo, DerivationStrictFrameInfo, GenericFrameInfo>;
using FrameStack = std::vector<FrameInfo>;

/**
 * Stack sampling profiler.
 */
class SampleStack : public EvalProfiler
{
    /* How often stack profiles should be flushed to file. This avoids the need
       to persist stack samples across the whole evaluation at the cost
       of periodically flushing data to disk. */
    static constexpr std::chrono::microseconds profileDumpInterval = std::chrono::milliseconds(2000);

    Hooks getNeededHooksImpl() const override
    {
        return Hooks().set(preFunctionCall).set(postFunctionCall);
    }

    FrameInfo getPrimOpFrameInfo(const PrimOp & primOp, std::span<Value *> args, PosIdx pos);

public:
    SampleStack(EvalState & state, std::filesystem::path profileFile, std::chrono::nanoseconds period)
        : state(state)
        , sampleInterval(period)
        , profileFd([&]() {
            AutoCloseFD fd = toDescriptor(open(profileFile.string().c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0660));
            if (!fd)
                throw SysError("opening file %s", profileFile);
            return fd;
        }())
        , posCache(state)
    {
    }

    [[gnu::noinline]] void
    preFunctionCallHook(EvalState & state, const Value & v, std::span<Value *> args, const PosIdx pos) override;
    [[gnu::noinline]] void
    postFunctionCallHook(EvalState & state, const Value & v, std::span<Value *> args, const PosIdx pos) override;

    void maybeSaveProfile(std::chrono::time_point<std::chrono::high_resolution_clock> now);
    void saveProfile();
    FrameInfo getFrameInfoFromValueAndPos(const Value & v, std::span<Value *> args, PosIdx pos);

    SampleStack(SampleStack &&) = default;
    SampleStack & operator=(SampleStack &&) = delete;
    SampleStack(const SampleStack &) = delete;
    SampleStack & operator=(const SampleStack &) = delete;
    ~SampleStack();
private:
    /** Hold on to an instance of EvalState for symbolizing positions. */
    EvalState & state;
    std::chrono::nanoseconds sampleInterval;
    AutoCloseFD profileFd;
    FrameStack stack;
    std::map<FrameStack, uint32_t> callCount;
    std::chrono::time_point<std::chrono::high_resolution_clock> lastStackSample =
        std::chrono::high_resolution_clock::now();
    std::chrono::time_point<std::chrono::high_resolution_clock> lastDump = std::chrono::high_resolution_clock::now();
    PosCache posCache;
};

FrameInfo SampleStack::getPrimOpFrameInfo(const PrimOp & primOp, std::span<Value *> args, PosIdx pos)
{
    auto derivationInfo = [&]() -> std::optional<FrameInfo> {
        /* Here we rely a bit on the implementation details of libexpr/primops/derivation.nix
           and derivationStrict primop. This is not ideal, but is necessary for
           the usefulness of the profiler. This might actually affect the evaluation,
           but the cost shouldn't be that high as to make the traces entirely inaccurate. */
        if (primOp.name == "derivationStrict") {
            try {
                /* Error context strings don't actually matter, since we ignore all eval errors. */
                state.forceAttrs(*args[0], pos, "");
                auto attrs = args[0]->attrs();
                auto nameAttr = state.getAttr(state.s.name, attrs, "");
                auto drvName = std::string(state.forceStringNoCtx(*nameAttr->value, pos, ""));
                return DerivationStrictFrameInfo{.callPos = pos, .drvName = std::move(drvName)};
            } catch (...) {
                /* Ignore all errors, since those will be diagnosed by the evaluator itself. */
            }
        }

        return std::nullopt;
    }();

    return derivationInfo.value_or(PrimOpFrameInfo{.expr = &primOp, .callPos = pos});
}

FrameInfo SampleStack::getFrameInfoFromValueAndPos(const Value & v, std::span<Value *> args, PosIdx pos)
{
    /* NOTE: No actual references to garbage collected values are not held in
       the profiler. */
    if (v.isLambda())
        return LambdaFrameInfo{.expr = v.lambda().fun, .callPos = pos};
    else if (v.isPrimOp()) {
        return getPrimOpFrameInfo(*v.primOp(), args, pos);
    } else if (v.isPrimOpApp())
        /* Resolve primOp eagerly. Must not hold on to a reference to a Value. */
        return PrimOpFrameInfo{.expr = v.primOpAppPrimOp(), .callPos = pos};
    else if (state.isFunctor(v)) {
        const auto functor = v.attrs()->get(state.s.functor);
        if (auto pos_ = posCache.lookup(pos); std::holds_alternative<std::monostate>(pos_.origin))
            /* HACK: In case callsite position is unresolved. */
            return FunctorFrameInfo{.pos = functor->pos};
        return FunctorFrameInfo{.pos = pos};
    } else
        /* NOTE: Add a stack frame even for invalid cases (e.g. when calling a non-function). This is what
         * trace-function-calls does. */
        return GenericFrameInfo{.pos = pos};
}

[[gnu::noinline]] void
SampleStack::preFunctionCallHook(EvalState & state, const Value & v, std::span<Value *> args, const PosIdx pos)
{
    stack.push_back(getFrameInfoFromValueAndPos(v, args, pos));

    auto now = std::chrono::high_resolution_clock::now();

    if (now - lastStackSample > sampleInterval) {
        callCount[stack] += 1;
        lastStackSample = now;
    }

    /* Do this in preFunctionCallHook because we might throw an exception, but
       callFunction uses Finally, which doesn't play well with exceptions. */
    maybeSaveProfile(now);
}

[[gnu::noinline]] void
SampleStack::postFunctionCallHook(EvalState & state, const Value & v, std::span<Value *> args, const PosIdx pos)
{
    if (!stack.empty())
        stack.pop_back();
}

std::ostream & LambdaFrameInfo::symbolize(const EvalState & state, std::ostream & os, PosCache & posCache) const
{
    if (auto pos = posCache.lookup(callPos); std::holds_alternative<std::monostate>(pos.origin))
        /* HACK: To avoid dubious «none»:0 in the generated profile if the origin can't be resolved
           resort to printing the lambda location instead of the callsite position. */
        os << posCache.lookup(expr->getPos());
    else
        os << pos;
    if (expr->name)
        os << ":" << state.symbols[expr->name];
    return os;
}

std::ostream & GenericFrameInfo::symbolize(const EvalState & state, std::ostream & os, PosCache & posCache) const
{
    os << posCache.lookup(pos);
    return os;
}

std::ostream & FunctorFrameInfo::symbolize(const EvalState & state, std::ostream & os, PosCache & posCache) const
{
    os << posCache.lookup(pos) << ":functor";
    return os;
}

std::ostream & PrimOpFrameInfo::symbolize(const EvalState & state, std::ostream & os, PosCache & posCache) const
{
    /* Sometimes callsite position can have an unresolved origin, which
       leads to confusing «none»:0 locations in the profile. */
    auto pos = posCache.lookup(callPos);
    if (!std::holds_alternative<std::monostate>(pos.origin))
        os << posCache.lookup(callPos) << ":";
    os << *expr;
    return os;
}

std::ostream &
DerivationStrictFrameInfo::symbolize(const EvalState & state, std::ostream & os, PosCache & posCache) const
{
    /* Sometimes callsite position can have an unresolved origin, which
       leads to confusing «none»:0 locations in the profile. */
    auto pos = posCache.lookup(callPos);
    if (!std::holds_alternative<std::monostate>(pos.origin))
        os << posCache.lookup(callPos) << ":";
    os << "primop derivationStrict:" << drvName;
    return os;
}

void SampleStack::maybeSaveProfile(std::chrono::time_point<std::chrono::high_resolution_clock> now)
{
    if (now - lastDump >= profileDumpInterval)
        saveProfile();
    else
        return;

    /* Save the last dump timepoint. Do this after actually saving data to file
       to not account for the time doing the flushing to disk. */
    lastDump = std::chrono::high_resolution_clock::now();

    /* Free up memory used for stack sampling. This might be very significant for
       long-running evaluations, so we shouldn't hog too much memory. */
    callCount.clear();
}

void SampleStack::saveProfile()
{
    auto os = std::ostringstream{};
    for (auto & [stack, count] : callCount) {
        auto first = true;
        for (auto & pos : stack) {
            if (first)
                first = false;
            else
                os << ";";

            std::visit([&](auto && info) { info.symbolize(state, os, posCache); }, pos);
        }
        os << " " << count;
        writeLine(profileFd.get(), os.str());
        /* Clear ostringstream. */
        os.str("");
        os.clear();
    }
}

SampleStack::~SampleStack()
{
    /* Guard against cases when we are already unwinding the stack. */
    try {
        saveProfile();
    } catch (...) {
        ignoreExceptionInDestructor();
    }
}

} // namespace

ref<EvalProfiler> makeSampleStackProfiler(EvalState & state, std::filesystem::path profileFile, uint64_t frequency)
{
    /* 0 is a special value for sampling stack after each call. */
    std::chrono::nanoseconds period = frequency == 0
                                          ? std::chrono::nanoseconds{0}
                                          : std::chrono::nanoseconds{std::nano::den / frequency / std::nano::num};
    return make_ref<SampleStack>(state, profileFile, period);
}

} // namespace nix
