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

* Show source origins recursively for all dependencies:

  ```console
  # nix derivation source-origins -r .#default
  ```

# Description

This command evaluates the given [*installables*](./nix.md#installables)
and, for each resulting [store derivation], prints a JSON object mapping
every `inputSrc` store path back to the original filesystem path that was
copied into the store during evaluation.

This is useful for build-system tooling that needs to know which working-tree
directories contributed to a derivation's build inputs — for example, to
determine which parts of a monorepo are affected by a change.

The `sourcePath` field is `null` for store paths that were not produced by
the current evaluation (e.g. paths from substituters or previous builds).

[store derivation]: @docroot@/glossary.md#gloss-store-derivation

)"" 
