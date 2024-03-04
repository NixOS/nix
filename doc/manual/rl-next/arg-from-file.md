---
synopsis: "CLI options `--arg-from-file` and `--arg-from-stdin`"
prs: 10122
---

The new CLI option `--arg-from-file` *name* *path* passes the contents
of file *path* as a string value via the function argument *name* to a
Nix expression. Similarly, the new option `--arg-from-stdin` *name*
reads the contents of the string from standard input.
