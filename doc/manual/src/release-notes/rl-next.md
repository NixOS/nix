# Release 2.5 (2021-XX-XX)

* Binary cache stores now have a setting `compression-level`.

* `nix develop` now has a flag `--unpack` to run `unpackPhase`.

* Lists can now be compared lexicographically using the `<` operator.

* New built-in function: `builtins.groupBy`, with the same functionality as
  Nixpkgs' `lib.groupBy`, but faster.

* Nix now searches for a flake.nix up until git or filesystem boundary.
