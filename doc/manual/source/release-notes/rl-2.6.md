# Release 2.6 (2022-01-24)

* The Nix CLI now searches for a `flake.nix` up until the root of the current
  Git repository or a filesystem boundary rather than just in the current
  directory.
* The TOML parser used by `builtins.fromTOML` has been replaced by [a
  more compliant one](https://github.com/ToruNiina/toml11).
* Added `:st`/`:show-trace` commands to `nix repl`, which are used to
  set or toggle display of error traces.
* New builtin function `builtins.zipAttrsWith` with the same
  functionality as `lib.zipAttrsWith` from Nixpkgs, but much more
  efficient.
* New command `nix store copy-log` to copy build logs from one store
  to another.
* The `commit-lockfile-summary` option can be set to a non-empty
  string to override the commit summary used when commiting an updated
  lockfile.  This may be used in conjunction with the `nixConfig`
  attribute in `flake.nix` to better conform to repository
  conventions.
* `docker run -ti nixos/nix:master` will place you in the Docker
  container with the latest version of Nix from the `master` branch.
