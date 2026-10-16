# An `abort` in the `context` argument of `addErrorContext` must not replace the
# `throw` in the `value` argument, so `tryEval` still catches the `throw`.
(builtins.tryEval (builtins.addErrorContext (abort "secondary") (throw "important"))).success
