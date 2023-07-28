# Release X.Y (202?-??-??)

- Two new builtin functions,
  [`builtins.parseFlakeRef`](@docroot@/language/builtins.md#builtins-parseFlakeRef)
  and
  [`builtins.flakeRefToString`](@docroot@/language/builtins.md#builtins-flakeRefToString),
  have been added.
  These functions are useful for converting between flake references encoded as attribute sets and URLs.

- [`builtins.toJSON`](@docroot@/language/builtins.md#builtins-parseFlakeRef) now prints [--show-trace](@docroot@/command-ref/conf-file.html#conf-show-trace) items for the path in which it finds an evaluation error.

- Error messages regarding malformed input to [`derivation add`](@docroot@/command-ref/new-cli/nix3-derivation-add.md) are now clearer and more detailed.

- The `discard-references` feature has been stabilized.
  This means that the
  [unsafeDiscardReferences](@docroot@/contributing/experimental-features.md#xp-feature-discard-references)
  attribute is no longer guarded by an experimental flag and can be used
  freely.

- The JSON output for derived paths with are store paths is now a string, not an object with a single `path` field.
  This only affects `nix-build --json` when "building" non-derivation things like fetched sources, which is a no-op.

- Introduce a new [`outputOf`](@docroot@/language/builtins.md#builtins-outputOf) builtin.
  It is part of the [`dynamic-derivations`](@docroot@/contributing/experimental-features.md#xp-feature-dynamic-derivations) experimental feature.

- [`derivation add` can add multiple derivations at once by reading the output of `derivation show`](@docroot@/command-ref/new-cli/nix3-derivation-add.md)
