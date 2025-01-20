---
synopsis: "Support for relative path inputs"
prs: [10089]
---

Flakes can now refer to other flakes in the same repository using relative paths, e.g.
```nix
inputs.foo.url = "path:./foo";
```
uses the flake in the `foo` subdirectory of the referring flake. For more information, see the documentation on [the `path` flake input type](@docroot@/command-ref/new-cli/nix3-flake.md#path-fetcher).

This feature required a change to the lock file format. Previous Nix versions will not be able to use lock files that have locks for relative path inputs in them.
