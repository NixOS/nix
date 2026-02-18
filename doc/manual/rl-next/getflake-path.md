---
synopsis: "`builtins.getFlake` now supports path values"
prs: [15290]
---

`builtins.getFlake` now accepts path values in addition to flakerefs, allowing you to write `builtins.getFlake ./subflake` instead of having to use ugly workarounds to construct a pure flakeref.
