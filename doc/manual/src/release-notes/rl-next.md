# Release X.Y (202?-??-??)

* Nix can now be built with LTO by passing `--enable-lto` to `configure`.
  LTO is currently only supported when building with GCC.

* `nix repl` now takes installables on the command line, unifying the usage
  with other commands that use `--file` and `--expr`. Primary breaking change
  is for the common usage of `nix repl '<nixpkgs>'` which can be recovered with
  `nix repl --file '<nixpkgs>'` or `nix repl --expr 'import <nixpkgs>{}'`
