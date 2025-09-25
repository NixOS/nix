R""(

# Examples

* Export the closure of building `nixpkgs#hello`:

  ```console
  # nix nario export --format 1 -r nixpkgs#hello > dump
  ```

  It can be imported in another store:

  ```console
  # nix nario import < dump
  ```

# Description

This command prints on standard output a serialization of the specified store paths in `nario` format. This serialization can be imported into another store using `nix nario import`.

References of a path are not exported by default; use `-r` to export a complete closure.
Paths are exported in topographically sorted order (i.e. if path `X` refers to `Y`, then `Y` appears before `X`).

You must specify the desired `nario` version. Currently the following versions are supported:

* `1`: This version is compatible with the legacy `nix-store --export` and `nix-store --import` commands.

)""
