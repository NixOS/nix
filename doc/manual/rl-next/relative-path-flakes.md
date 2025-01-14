---
synopsis: "Support for relative path inputs"
prs: [10089]
---

Flakes can now refer to other flakes in the same repository using relative paths, e.g.
```nix
inputs.foo.url = "path:./foo";
```
uses the flake in the `foo` subdirectory of the referring flake.

This feature required a change to the lock file format. Previous Nix versions will not be able to use lock files that have locks for relative path inputs in them.
