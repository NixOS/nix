---
synopsis: "`inherit (x) ...` evalutates `x` only once"
prs: 9847
---

`inherit (x) a b ...` now evalutates the expression `x` only once for all inherited attributes rather than once for each inherited attribute.
This does not usually have a measurable impact, but side-effects (such as `builtins.trace`) would be duplicated and expensive expressions (such as derivations) could cause a measurable slowdown.
