# Release 2.8 (2022-04-19)

* New experimental command: `nix fmt`, which applies a formatter
  defined by the `formatter.<system>` flake output to the Nix
  expressions in a flake.

* Various Nix commands can now read expressions from standard input
  using `--file -`.

* New experimental builtin function `builtins.fetchClosure` that
  copies a closure from a binary cache at evaluation time and rewrites
  it to content-addressed form (if it isn't already). Like
  `builtins.storePath`, this allows importing pre-built store paths;
  the difference is that it doesn't require the user to configure
  binary caches and trusted public keys.

  This function is only available if you enable the experimental
  feature `fetch-closure`.

* New experimental feature: *impure derivations*. These are
  derivations that can produce a different result every time they're
  built. Here is an example:

  ```nix
  stdenv.mkDerivation {
    name = "impure";
    __impure = true; # marks this derivation as impure
    buildCommand = "date > $out";
  }
  ```

  Running `nix build` twice on this expression will build the
  derivation twice, producing two different content-addressed store
  paths. Like fixed-output derivations, impure derivations have access
  to the network. Only fixed-output derivations and impure derivations
  can depend on an impure derivation.

* `nix store make-content-addressable` has been renamed to `nix store
  make-content-addressed`.

* The `nixosModule` flake output attribute has been renamed consistent
  with the `.default` renames in Nix 2.7.

  * `nixosModule` → `nixosModules.default`

  As before, the old output will continue to work, but `nix flake check` will
  issue a warning about it.

* `nix run` is now stricter in what it accepts: members of the `apps`
  flake output are now required to be apps (as defined in [the
  manual](https://nix.dev/manual/nix/stable/command-ref/new-cli/nix3-run.html#apps)),
  and members of `packages` or `legacyPackages` must be derivations
  (not apps).
