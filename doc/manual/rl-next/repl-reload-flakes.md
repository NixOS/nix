---
synopsis: "REPL `:load-flake` and `:reload` now work together"
issues: [8753]
prs: [13180]
---

Previously, `:reload` only reloaded the files specified with `:load` (or on the command line).
Now, it also works with the flakes specified with `:load-flake` (or on the command line).
This makes it correctly reload everything that was previously loaded, regardless of what sort of thing (plain file or flake) each item is.
