# Release 2.5 (2021-XX-XX)

* Binary cache stores now have a setting `compression-level`.

* `nix develop` now has a flag `--unpack` to run `unpackPhase`.

* nix subcommands operating on a Flake will search for a flake.nix in a parent
  directory if no path is explicitly given.

* Lists can now be compared lexicographically using the `<` operator.
