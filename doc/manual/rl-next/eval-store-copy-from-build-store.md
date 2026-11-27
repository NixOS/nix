---
synopsis: "With `--eval-store`, `builtins.storePath`, `builtins.appendContext` and pinned `builtins.fetchurl` copy paths from the build store"
prs: [16855]
---

With a separate evaluation store (`--eval-store`), `builtins.storePath` and `builtins.appendContext` failed on a path that only the build store had, with "is required, but there is no substituter that can build it".
`builtins.fetchurl` and `builtins.fetchTarball` with a hash downloaded such a path again, and failed if the URL was unavailable.

These builtins still try to substitute the path into the evaluation store first.
If that fails and the build store has the path, they now copy its closure from the build store, as import-from-derivation already does for its outputs.
The copy checks signatures as the evaluation store requires, so an evaluation store that requires signatures refuses an unsigned input-addressed path with "lacks a signature by a trusted key".
