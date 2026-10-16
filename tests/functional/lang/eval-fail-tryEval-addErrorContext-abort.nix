# `tryEval` does not catch `abort`. An error from the `context` argument of
# `addErrorContext` must not turn the `abort` into an error that `tryEval`
# catches.
builtins.tryEval (builtins.addErrorContext "${throw "x"}" (abort "real"))
