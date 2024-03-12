#pragma once

#include <algorithm>

#include "error.hh"
#include "pos-idx.hh"
#include "print-options.hh"
#include "print.hh"
#include "value/context.hh"

namespace nix {

struct Env;
struct Expr;
struct Value;

class EvalState;
template<class T>
class EvalErrorBuilder;

class EvalError : public Error
{
    template<class T>
    friend class EvalErrorBuilder;
public:
    EvalState & state;

    EvalError(EvalState & state, ErrorInfo && errorInfo)
        : Error(errorInfo)
        , state(state)
    {
    }

    template<typename... Args>
    explicit EvalError(EvalState & state, const std::string & formatString, const Args &... formatArgs)
        : Error(formatString, formatArgs...)
        , state(state)
    {
    }
};

MakeError(ParseError, Error);
MakeError(AssertionError, EvalError);
MakeError(ThrownError, AssertionError);
MakeError(Abort, EvalError);
MakeError(TypeError, EvalError);
MakeError(UndefinedVarError, EvalError);
MakeError(MissingArgumentError, EvalError);
MakeError(CachedEvalError, EvalError);
MakeError(InfiniteRecursionError, EvalError);

struct InvalidPathError : public EvalError
{
public:
    Path path;
    InvalidPathError(EvalState & state, const Path & path)
        : EvalError(state, "path '%s' is not valid", path)
    {
    }
};

struct PoisonContextError : public EvalError
{
public:
    std::shared_ptr<Value> value;
    const Poison & poison;

    PoisonContextError(EvalState & state, const Poison & poison)
        : EvalError(state, "Value is poisoned from %1% and may not be built or included in derivations", poison)
        , value(nullptr)
        , poison(poison)
    {
    }

    PoisonContextError(EvalState & state, const Poison & poison, Value * value)
        : EvalError(state, "Value is poisoned from %1% and may not be built or included in derivations: %2%", poison, ValuePrinter(state, *value, errorPrintOptions))
        , value(value)
        , poison(poison)
    {
    }
};

/**
 * `EvalErrorBuilder`s may only be constructed by `EvalState`. The `debugThrow`
 * method must be the final method in any such `EvalErrorBuilder` usage, and it
 * handles deleting the object.
 */
template<class T>
class EvalErrorBuilder final
{
    friend class EvalState;

    template<typename... Args>
    explicit EvalErrorBuilder(EvalState & state, Args &&... args)
        : error(T(state, args...))
    {
    }

public:
    T error;

    [[nodiscard, gnu::noinline]] EvalErrorBuilder<T> & withExitStatus(unsigned int exitStatus);

    [[nodiscard, gnu::noinline]] EvalErrorBuilder<T> & atPos(PosIdx pos);

    [[nodiscard, gnu::noinline]] EvalErrorBuilder<T> & atPos(Value & value, PosIdx fallback = noPos);

    [[nodiscard, gnu::noinline]] EvalErrorBuilder<T> & withTrace(PosIdx pos, const std::string_view text);

    [[nodiscard, gnu::noinline]] EvalErrorBuilder<T> & withFrameTrace(PosIdx pos, const std::string_view text);

    [[nodiscard, gnu::noinline]] EvalErrorBuilder<T> & withSuggestions(Suggestions & s);

    [[nodiscard, gnu::noinline]] EvalErrorBuilder<T> & withFrame(const Env & e, const Expr & ex);

    [[nodiscard, gnu::noinline]] EvalErrorBuilder<T> & addTrace(PosIdx pos, HintFmt hint);

    template<typename... Args>
    [[nodiscard, gnu::noinline]] EvalErrorBuilder<T> &
    addTrace(PosIdx pos, std::string_view formatString, const Args &... formatArgs);

    /**
     * Delete the `EvalErrorBuilder` and throw the underlying exception.
     */
    [[gnu::noinline, gnu::noreturn]] void debugThrow();
};

}
