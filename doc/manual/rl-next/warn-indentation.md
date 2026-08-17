---
synopsis: "`builtins.warn` now supports unindented multi-line messages"
prs: [14794]
---

`builtins.warn` now preserves the formatting of messages that start with a newline instead of adding indentation to align them with the warning prefix.
