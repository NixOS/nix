---
synopsis: Add `builtins.toStringDebug`
prs:
---

Added `builtins.toStringDebug`, which formats a value as a string for debugging
purposes. Unlike `builtins.toString`, `builtins.toStringDebug` will never error
and will always produce human-readable, pretty-printed output (including for
expressions that error). This makes it ideal for interpolation into
`builtins.trace` calls and `assert` messages.

A variant, `builtins.toStringDebugOptions`, accepts as its first argument a set
of options for additional control over the output.
