#include "nix/expr/eval-error.hh"
#include "nix/expr/eval.hh"
#include "nix/expr/value.hh"
#include "nix/store/store-api.hh"

namespace nix {

InvalidPathError::InvalidPathError(EvalState & state, const StorePath & path)
    : EvalError(state, "path '%s' is not valid", state.store->printStorePath(path))
    , path{path}
{
}

template<class T>
EvalErrorBuilder<T> & EvalErrorBuilder<T>::withExitStatus(unsigned int exitStatus)
{
    error.withExitStatus(exitStatus);
    return *this;
}

template<class T>
EvalErrorBuilder<T> & EvalErrorBuilder<T>::atPos(PosIdx pos)
{
    error.err.pos = error.state.positions[pos];
    return *this;
}

template<class T>
EvalErrorBuilder<T> & EvalErrorBuilder<T>::atPos(Value & value, PosIdx fallback)
{
    return atPos(value.determinePos(fallback));
}

template<class T>
EvalErrorBuilder<T> & EvalErrorBuilder<T>::withTrace(PosIdx pos, const std::string_view text)
{
    error.addTrace(error.state.positions[pos], text);
    return *this;
}

template<class T>
EvalErrorBuilder<T> & EvalErrorBuilder<T>::withSuggestions(Suggestions & s)
{
    error.err.suggestions = s;
    return *this;
}

template<class T>
EvalErrorBuilder<T> & EvalErrorBuilder<T>::withFrame(const Env & env, const Expr & expr)
{
    // NOTE: This is abusing side-effects.
    // TODO: check compatibility with nested debugger calls.
    // TODO: What side-effects??
    error.state.debugTraces.push_front(
        DebugTrace{
            .pos = expr.getPos(),
            .expr = expr,
            .env = env,
            .hint = HintFmt("Fake frame for debugging purposes"),
            .isError = true});
    return *this;
}

template<class T>
EvalErrorBuilder<T> & EvalErrorBuilder<T>::addTrace(PosIdx pos, HintFmt hint)
{
    error.addTrace(error.state.positions[pos], hint);
    return *this;
}

template<class T>
template<typename... Args>
EvalErrorBuilder<T> &
EvalErrorBuilder<T>::addTrace(PosIdx pos, std::string_view formatString, const Args &... formatArgs)
{

    addTrace(error.state.positions[pos], HintFmt(std::string(formatString), formatArgs...));
    return *this;
}

template<class T>
EvalErrorBuilder<T> & EvalErrorBuilder<T>::setIsFromExpr()
{
    error.err.isFromExpr = true;
    return *this;
}

template<class T>
void EvalErrorBuilder<T>::debugThrow()
{
    error.state.runDebugRepl(&error);

    // `EvalState` is the only class that can construct an `EvalErrorBuilder`,
    // and it does so in dynamic storage. This is the final method called on
    // any such instance and must delete itself before throwing the underlying
    // error.
    auto error = std::move(this->error);
    delete this;

    throw error;
}

template<class T>
void EvalErrorBuilder<T>::panic()
{
    logError(error.info());
    printError(
        "This is a bug! An unexpected condition occurred, causing the Nix evaluator to have to stop. If you could share a reproducible example or a core dump, please open an issue at https://github.com/NixOS/nix/issues");
    abort();
}

template class EvalErrorBuilder<EvalBaseError>;
template class EvalErrorBuilder<EvalError>;
template class EvalErrorBuilder<AssertionError>;
template class EvalErrorBuilder<ThrownError>;
template class EvalErrorBuilder<Abort>;
template class EvalErrorBuilder<TypeError>;
template class EvalErrorBuilder<UndefinedVarError>;
template class EvalErrorBuilder<MissingArgumentError>;
template class EvalErrorBuilder<InfiniteRecursionError>;
template class EvalErrorBuilder<StackOverflowError>;
template class EvalErrorBuilder<InvalidPathError>;
template class EvalErrorBuilder<IFDError>;

} // namespace nix
