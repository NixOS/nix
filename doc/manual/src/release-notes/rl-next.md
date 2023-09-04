# Release X.Y (202?-??-??)

- Two new builtin functions,
  [`builtins.parseFlakeRef`](@docroot@/language/builtins.md#builtins-parseFlakeRef)
  and
  [`builtins.flakeRefToString`](@docroot@/language/builtins.md#builtins-flakeRefToString),
  have been added.
  These functions are useful for converting between flake references encoded as attribute sets and URLs.

- Function introspection in the language has been extended to allow more compatibility logic to be written.

  - [`builtins.functionOpen`](@docroot@/language/builtins.md#builtins-functionOpen): whether arbitrary attributes can be passed to the function; a boolean, or `null` for plain lambdas like `x: x`.
  - [`builtins.functionBindsAllAttrs`](@docroot@/language/builtins.md#builtins-functionBindsAllAttrs): whether the function puts the whole attrset into a variable with `@`.

  The combination of being closed, but binding all attributes is not forward compatible and can now be reported as part of migrations that add an attribute to a function call.

- [`builtins.functionStrict`](@docroot@/language/builtins.md#builtins-functionStrict): whether the function is written using strict syntax, such as `{ ... }: foo`. The fact that this function is strict in its argument is often forgotten, so this allows library or DSL authors to detect and report it in places where it might be common to encounter this.

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

- Flake follow paths at depths greater than 2 are now handled correctly, preventing "follows a non-existent input" errors.

- [`nix-store --query`](@docroot@/command-ref/nix-store/query.md) gained a new type of query: `--valid-derivers`. It returns all `.drv` files in the local store that *can be* used to build the output passed in argument.
This is in contrast to `--deriver`, which returns the single `.drv` file that *was actually* used to build the output passed in argument. In case the output was substituted from a binary cache,
this `.drv` file may only exist on said binary cache and not locally.
