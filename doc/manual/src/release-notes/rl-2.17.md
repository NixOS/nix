# Release 2.17 (2023-07-24)

* [`nix-channel`](../command-ref/nix-channel.md) now supports a `--list-generations` subcommand.

* The function [`builtins.fetchClosure`](../language/builtins.md#builtins-fetchClosure) can now fetch input-addressed paths in [pure evaluation mode](../command-ref/conf-file.md#conf-pure-eval), as those are not impure.

* Nix now allows unprivileged/[`allowed-users`](../command-ref/conf-file.md#conf-allowed-users) to sign paths.
  Previously, only [`trusted-users`](../command-ref/conf-file.md#conf-trusted-users) users could sign paths.

* Nested dynamic attributes are now merged correctly by the parser. For example:

  ```nix
  {
    nested = {
      foo = 1;
    };
    nested = {
      ${"ba" + "r"} = 2;
    };
  }
  ```

  This used to silently discard `nested.bar`, but now behaves as one would expect and evaluates to:

  ```nix
  { nested = { bar = 2; foo = 1; }; }
  ```

  Note that the feature of merging multiple *full declarations* of attribute sets like `nested` in the example is of questionable value.
  It allows writing expressions that are very hard to read, for instance when there are many lines of code between two declarations of the same attribute.
  This has been around for a long time and is therefore supported for backwards compatibility, but should not be relied upon.

  Instead, consider using the *nested attribute path* syntax:

  ```nix
  {
    nested.foo = 1;
    nested.${"ba" + "r"} = 2;
  }
  ```

* Tarball flakes can now redirect to an "immutable" URL that will be recorded in lock files. This allows the use of "mutable" tarball URLs like `https://example.org/hello/latest.tar.gz` in flakes. See the [tarball fetcher](../protocols/tarball-fetcher.md) for details.
