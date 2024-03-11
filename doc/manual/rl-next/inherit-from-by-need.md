---
synopsis: "`inherit (x) ...` evaluates `x` only once"
prs: 9847
---

`inherit (x) a b ...` now evaluates the expression `x` only once for all inherited attributes rather than once for each inherited attribute.
This does not usually have a measurable impact, but side-effects (such as `builtins.trace`) would be duplicated and expensive expressions (such as derivations) could cause a measurable slowdown.
