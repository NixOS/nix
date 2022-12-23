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

* Allow explicitly selecting outputs in a store derivation installable, just like we can do with other sorts of installables.
  For example,
  ```shell-session
  $ nix-build /nix/store/gzaflydcr6sb3567hap9q6srzx8ggdgg-glibc-2.33-78.drv^dev`
  ```
  now works just as
  ```shell-session
  $ nix-build glibc^dev`
  ```
  does already.

* On Linux, `nix develop` now sets the
  [*personality*](https://man7.org/linux/man-pages/man2/personality.2.html)
  for the development shell in the same way as the actual build of the
  derivation. This makes shells for `i686-linux` derivations work
  correctly on `x86_64-linux`.
