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

- `nix search` now omits `{packages.legacyPackages}.${system}` from the attribute path
  Specifically, those attribute path prefixes will no longer match the search
  query, nor will they show up in the search results.
