# Release 2.22.0 (2024-04-23)

### Significant changes

- Remove experimental repl-flake [#10103](https://github.com/NixOS/nix/issues/10103) [#10299](https://github.com/NixOS/nix/pull/10299)

  The `repl-flake` experimental feature has been removed. The `nix repl` command now works like the rest of the new CLI in that `nix repl {path}` now tries to load a flake at `{path}` (or fails if the `flakes` experimental feature isn't enabled).

### Other changes

- `nix eval` prints derivations as `.drv` paths [#10200](https://github.com/NixOS/nix/pull/10200)

  `nix eval` will now print derivations as their `.drv` paths, rather than as
  attribute sets. This makes commands like `nix eval nixpkgs#bash` terminate
  instead of infinitely looping into recursive self-referential attributes:

  ```ShellSession
  $ nix eval nixpkgs#bash
  «derivation /nix/store/m32cbgbd598f4w299g0hwyv7gbw6rqcg-bash-5.2p26.drv»
  ```

