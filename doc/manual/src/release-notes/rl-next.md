# Release X.Y (202?-??-??)

* `nix repl` has a new build-'n-link (`:bl`) command that builds a derivation
  while creating GC root symlinks.

* The path produced by `builtins.toFile` is now allowed to be imported or read
  even with restricted evaluation. Note that this will not work with a
  read-only store.

* `nix build` has a new `--print-out-paths` flag to print the resulting output paths.
  This matches the default behaviour of `nix-build`.

* Nix can now be built with LTO by passing `--enable-lto` to `configure`.
  LTO is currently only supported when building with GCC.