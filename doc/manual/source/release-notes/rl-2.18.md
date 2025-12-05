# Release 2.18 (2023-09-20)

- Two new builtin functions,
  [`builtins.parseFlakeRef`](@docroot@/language/builtins.md#builtins-parseFlakeRef)
  and
  [`builtins.flakeRefToString`](@docroot@/language/builtins.md#builtins-flakeRefToString),
  have been added.
  These functions are useful for converting between flake references encoded as attribute sets and URLs.

- [`builtins.toJSON`](@docroot@/language/builtins.md#builtins-parseFlakeRef) now prints [--show-trace](@docroot@/command-ref/conf-file.html#conf-show-trace) items for the path in which it finds an evaluation error.

- Error messages regarding malformed input to [`nix derivation add`](@docroot@/command-ref/new-cli/nix3-derivation-add.md) are now clearer and more detailed.

- The `discard-references` feature has been stabilized.
  This means that the
  [unsafeDiscardReferences](@docroot@/language/advanced-attributes.md#adv-attr-unsafeDiscardReferences)
  attribute is no longer guarded by an experimental flag and can be used
  freely.

- The JSON output for derived paths which are store paths is now a string, not an object with a single `path` field.
  This only affects `nix-build --json` when "building" non-derivation things like fetched sources, which is a no-op.

- A new builtin [`outputOf`](@docroot@/language/builtins.md#builtins-outputOf) has been added.
  It is part of the [`dynamic-derivations`](@docroot@/development/experimental-features.md#xp-feature-dynamic-derivations) experimental feature.

- Flake follow paths at depths greater than 2 are now handled correctly, preventing "follows a non-existent input" errors.

- [`nix-store --query`](@docroot@/command-ref/nix-store/query.md) gained a new type of query: `--valid-derivers`. It returns all `.drv` files in the local store that *can be* used to build the output passed in argument. This is in contrast to `--deriver`, which returns the single `.drv` file that *was actually* used to build the output passed in argument. In case the output was substituted from a binary cache, this `.drv` file may only exist on said binary cache and not locally.
