---
synopsis: Enter the `--debugger` when `builtins.trace` is called if `builtins-trace-debugger` is set
prs: 9914
---

If the `builtins-trace-debugger` option is set and `--debugger` is given,
`builtins.trace` calls will behave similarly to `builtins.break` and will enter
the debug REPL. This is useful for determining where warnings are being emitted
from.
