# Release X.Y (202?-??-??)

* The Nix cli now searches for a flake.nix up until the root of the current git repository or a filesystem boundary rather than just in the current directory
* The TOML parser used by `builtins.fromTOML` has been replaced by [a
  more compliant one](https://github.com/ToruNiina/toml11).
