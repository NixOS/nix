# `tryEval` does not catch type errors. An error from the `context` argument of
# `addErrorContext` must not turn a type error into an error that `tryEval`
# catches.
builtins.tryEval (builtins.addErrorContext (throw "x") (1 + "a"))
