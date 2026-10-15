# `addErrorContext` evaluates its `context` argument only if evaluating its
# `value` argument fails.
builtins.addErrorContext (throw "context evaluated") "ok"
