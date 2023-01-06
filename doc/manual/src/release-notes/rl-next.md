# Release X.Y (202?-??-??)

* The `repeat` and `enforce-determinism` options have been removed
  since they had been broken under many circumstances for a long time.

* You can now use [flake references] in the [old command line interface], e.g.

   [flake references]: ../command-ref/new-cli/nix3-flake.md#flake-references
   [old command line interface]: ../command-ref/main-commands.md

  ```
  # nix-build flake:nixpkgs -A hello
  # nix-build -I nixpkgs=flake:github:NixOS/nixpkgs/nixos-22.05 \
      '<nixpkgs>' -A hello
  # NIX_PATH=nixpkgs=flake:nixpkgs nix-build '<nixpkgs>' -A hello
  ```

* Instead of "antiquotation", the more common term [string interpolation](../language/string-interpolation.md) is now used consistently.
  Historical release notes were not changed.

* Error traces have been reworked to provide detailed explanations and more
  accurate error locations. A short excerpt of the trace is now shown by
  default when an error occurs.
