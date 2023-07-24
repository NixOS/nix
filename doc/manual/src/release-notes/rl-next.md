# Release X.Y (202?-??-??)

- [`nix-channel`](../command-ref/nix-channel.md) now supports a `--list-generations` subcommand

* The function [`builtins.fetchClosure`](../language/builtins.md#builtins-fetchClosure) can now fetch input-addressed paths in [pure evaluation mode](../command-ref/conf-file.md#conf-pure-eval), as those are not impure.

- Nix now allows unprivileged/[`allowed-users`](../command-ref/conf-file.md#conf-allowed-users) to sign paths.

- Two new builtin functions, `builtins.parseFlakeRef` and `builtins.flakeRefToString`, have been added.
  These functions are useful for converting between flake references encoded
  as attribute sets and URLs.

- Nested dynamic attributes are now merged correctly by the parser. For example:

  ```nix
  {
    nested = { foo = 1; };
    nested = { ${"ba" + "r"} = 2; };
  }
  ```

  This used to silently discard `nested.bar`, but now behaves as one would expect and evaluates to:

  ```nix
  { nested = { bar = 2; foo = 1; }; }
  ```

  Note that the feature of merging multiple attribute set declarations is of questionable value.
  It allows writing expressions that are very hard to read, for instance when there are many lines of code between two declarations of the same attribute.
  This has been around for a long time and is therefore supported for backwards compatibility, but should not be relied upon.
