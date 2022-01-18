# Release X.Y (202?-??-??)

* The Nix cli now searches for a flake.nix up until the root of the current git repository or a filesystem boundary rather than just in the current directory
* The TOML parser used by `builtins.fromTOML` has been replaced by [a
  more compliant one](https://github.com/ToruNiina/toml11).
* Added `:st`/`:show-trace` commands to nix repl, which are used to
  set or toggle display of error traces.
* New builtin function `builtins.zipAttrsWith` with same functionality
  as `lib.zipAttrsWith` from nixpkgs, but much more efficient.
* New command `nix store copy-log` to copy build logs from one store
  to another.
