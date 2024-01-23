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

    [[nodiscard, gnu::noinline]] EvalErrorBuilder<T> & addTrace(PosIdx pos, hintformat hint, bool frame = false);

    template<typename... Args>
    [[nodiscard, gnu::noinline]] EvalErrorBuilder<T> &
    addTrace(PosIdx pos, std::string_view formatString, const Args &... formatArgs);

    [[gnu::noinline, gnu::noreturn]] void debugThrow();
};

/**
 * The size needed to allocate any `EvalErrorBuilder<T>`.
 *
 * The list of classes here needs to be kept in sync with the list of `template
 * class` declarations in `eval-error.cc`.
 *
 * This is used by `EvalState` to preallocate a buffer of sufficient size for
 * any `EvalErrorBuilder<T>` to avoid allocating while evaluating Nix code.
 */
constexpr size_t EVAL_ERROR_BUILDER_SIZE = std::max({
    sizeof(EvalErrorBuilder<EvalError>),
    sizeof(EvalErrorBuilder<AssertionError>),
    sizeof(EvalErrorBuilder<ThrownError>),
    sizeof(EvalErrorBuilder<Abort>),
    sizeof(EvalErrorBuilder<TypeError>),
    sizeof(EvalErrorBuilder<UndefinedVarError>),
    sizeof(EvalErrorBuilder<MissingArgumentError>),
    sizeof(EvalErrorBuilder<InfiniteRecursionError>),
    sizeof(EvalErrorBuilder<CachedEvalError>),
    sizeof(EvalErrorBuilder<InvalidPathError>),
});

}
