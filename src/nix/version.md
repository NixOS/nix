R""(

# Examples

* Show the Nix version:

  ```console
  # nix version
  nix (Nix) X.Y.Z
  ```

* Show the version together with the versions of the libraries Nix
  links against, as machine-readable JSON:

  ```console
  # nix version --json
  {
    "version": "X.Y.Z",
    "libraries": {
      "libcurl": "P.Q.R",
      "libgit2": "A.B.C"
    }
  }
  ```

# Description

Print the version of Nix.

Without any flags, this is like the [`nix --version`](@docroot@/command-ref/opt-common.md#opt-version)
option.

This subcommand additionally reports the versions of the libraries Nix links against (see `--json` below).

With `--json`, the output is a JSON object with the following fields:

* `version`: The version of Nix, as also printed by
  [`nix --version`](@docroot@/command-ref/opt-common.md#opt-version).

* `libraries`: An object mapping the names of the libraries that this
  Nix executable links against to their runtime versions. Which
  libraries appear here depends on how Nix was built, so consumers
  should not assume any particular key is present.

  These are the *runtime* versions of the dynamically linked
  libraries, which *on some platforms* may differ from the versions Nix was
  compiled against.

  This list of libraries does not include transitive dependencies, nor those we
  don't expect to have significant user-visible differences between versions.

)""
