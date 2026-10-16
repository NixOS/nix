# An error from the `context` argument of `addErrorContext` must not replace the
# error from the `value` argument.
builtins.addErrorContext (throw "secondary") (throw "important")
