#pragma once

#include <algorithm>

#include "error.hh"
#include "pos-idx.hh"

namespace nix {

struct Env;
struct Expr;
struct Value;

class EvalState;
template<class T>
class EvalErrorBuilder;

/**
 * Base class for all errors that occur during evaluation.
 *
 * Most subclasses should inherit from `EvalError` instead of this class.
 */
class EvalBaseError : public Error
{
    template<class T>
    friend class EvalErrorBuilder;
public:
    EvalState & state;

    EvalBaseError(EvalState & state, ErrorInfo && errorInfo)
        : Error(errorInfo)
        , state(state)
    {
    }

    template<typename... Args>
    explicit EvalBaseError(EvalState & state, const std::string & formatString, const Args &... formatArgs)
        : Error(formatString, formatArgs...)
        , state(state)
    {
    }
};

/**
 * `EvalError` is the base class for almost all errors that occur during evaluation.
 *
 * All instances of `EvalError` should show a degree of purity that allows them to be
 * cached in pure mode. This means that they should not depend on the configuration or the overall environment.
 */
MakeError(EvalError, EvalBaseError);
MakeError(ParseError, Error);
MakeError(AssertionError, EvalError);
MakeError(ThrownError, AssertionError);
MakeError(Abort, EvalError);
MakeError(TypeError, EvalError);
MakeError(UndefinedVarError, EvalError);
MakeError(MissingArgumentError, EvalError);
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
    explicit EvalErrorBuilder(EvalState & state, const Args &... args)
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

    [[nodiscard, gnu::noinline]] EvalErrorBuilder<T> & setIsFromExpr();

    template<typename... Args>
    [[nodiscard, gnu::noinline]] EvalErrorBuilder<T> &
    addTrace(PosIdx pos, std::string_view formatString, const Args &... formatArgs);

    /**
     * Delete the `EvalErrorBuilder` and throw the underlying exception.
     */
    [[gnu::noinline, gnu::noreturn]] void debugThrow();
};

}
