# Release X.Y (202?-??-??)

* `nix repl` has a new build-'n-link (`:bl`) command that builds a derivation
  while creating GC root symlinks.

* `nix build` has a new `--print-out-paths` flag to print the resulting output paths.
  This matches the default behaviour of `nix-build`.
