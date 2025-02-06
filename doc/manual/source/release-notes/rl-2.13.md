# Release 2.13 (2023-01-17)

* The `repeat` and `enforce-determinism` options have been removed
  since they had been broken under many circumstances for a long time.

* You can now use [flake references] in the [old command line interface], e.g.

   [flake references]: ../command-ref/new-cli/nix3-flake.md#flake-references
   [old command line interface]: ../command-ref/main-commands.md

  ```shell-session
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

* Allow explicitly selecting outputs in a store derivation installable, just like we can do with other sorts of installables.
  For example,
  ```shell-session
  # nix build /nix/store/gzaflydcr6sb3567hap9q6srzx8ggdgg-glibc-2.33-78.drv^dev
  ```
  now works just as
  ```shell-session
  # nix build nixpkgs#glibc^dev
  ```
  does already.

* On Linux, `nix develop` now sets the
  [*personality*](https://man7.org/linux/man-pages/man2/personality.2.html)
  for the development shell in the same way as the actual build of the
  derivation. This makes shells for `i686-linux` derivations work
  correctly on `x86_64-linux`.

* You can now disable the global flake registry by setting the `flake-registry`
  configuration option to an empty string. The same can be achieved at runtime with
  `--flake-registry ""`.
