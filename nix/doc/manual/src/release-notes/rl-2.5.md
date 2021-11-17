# Release 2.5 (2021-12-13)

* The garbage collector no longer blocks new builds, so the message
  `waiting for the big garbage collector lock...` is a thing of the
  past.

* Binary cache stores now have a setting `compression-level`.

* `nix develop` now has a flag `--unpack` to run `unpackPhase`.

* Lists can now be compared lexicographically using the `<` operator.

* New built-in function: `builtins.groupBy`, with the same functionality as
  Nixpkgs' `lib.groupBy`, but faster.

* `nix repl` now has a `:log` command.
