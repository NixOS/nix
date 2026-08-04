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

* `libraries`: An object mapping the names of Nix's direct third-party
  dependencies to their versions. Which entries appear here depends on
  how Nix was built, so consumers should not assume any particular key
  is present.

  For a library that exposes its version at runtime, this is the version
  of the loaded library, which *on some platforms* may differ from the
  version Nix was compiled against. For dependencies without such a
  query, it is the version Nix was compiled with.

  This list covers direct dependencies only; transitive dependencies are
  not included.

)""
