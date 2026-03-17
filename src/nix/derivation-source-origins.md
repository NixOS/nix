R""(

# Examples

* Show the source origins for a flake's default package:

  ```console
  # nix derivation source-origins .#default
  {
    "/nix/store/...-my-project.drv": {
      "drvPath": "/nix/store/...-my-project.drv",
      "name": "my-project",
      "inputSrcs": {
        "/nix/store/...-source": {
          "storePath": "/nix/store/...-source",
          "sourcePath": "/home/user/my-project"
        }
      }
    }
  }
  ```

* Show source origins for a flake in a git monorepo (use `git+file://`
  scheme so nix resolves the git root correctly):

  ```console
  # nix derivation source-origins "git+file:///path/to/monorepo?dir=my/flake"
  ```

* When the store path exists on disk and its `sourcePath` is a directory,
  `sourceFiles` lists the individual files inside it:

  ```console
  # nix derivation source-origins .#default
  {
    "/nix/store/...-my-project.drv": {
      ...
      "inputSrcs": {
        "/nix/store/...-filtered-src": {
          "storePath": "/nix/store/...-filtered-src",
          "sourcePath": "/home/user/monorepo",
          "sourceFiles": [
            "/home/user/monorepo/my/flake/src/main.rs",
            "/home/user/monorepo/my/flake/Cargo.toml"
          ]
        }
      }
    }
  }
  ```

  This is especially useful for `cleanSourceWith` / `builtins.path` sources
  where the `sourcePath` is a broad directory (e.g. the monorepo root) but
  the store path only contains the files that passed the filter.

# Description

This command evaluates the given [*installables*](./nix.md#installables)
and, for each resulting [store derivation], prints a JSON object mapping
every `inputSrc` store path back to the original filesystem path that was
copied into the store during evaluation.

This is useful for build-system tooling that needs to know which working-tree
directories contributed to a derivation's build inputs — for example, to
determine which parts of a monorepo are affected by a change.

The mapping is populated from three sources during evaluation:

1. **`EvalState::storeToSrc`** — the reverse of `srcToStore`, populated
   alongside it in `copyPathToStore()`. Captures paths added via the
   normal evaluation path.

2. **`EvalState::sourceStoreToOriginalPath`** — records the original
   filesystem path for flake source store paths, set in `mountInput()`
   when flake inputs are mounted into the virtual filesystem.

3. **`recordPathOrigin()`** — called from `addPath()` to capture
   store→source mappings for `builtins.path` / `cleanSourceWith` sources,
   which bypass `copyPathToStore()`.

The `sourcePath` field is `null` for store paths that were not produced by
the current evaluation (e.g. paths from substituters or previous builds).

When `sourcePath` is a directory and the store path exists on disk, the
`sourceFiles` field lists the individual files inside the store path,
mapped back to their original source locations. The store path IS the
filtered result (for `cleanSourceWith`), so its contents are exactly the
files that passed the filter.

[store derivation]: @docroot@/glossary.md#gloss-store-derivation

)"" 
