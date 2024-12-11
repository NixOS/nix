# Release 2.15 (2023-04-11)

* Commands which take installables on the command line can now read them from the standard input if
  passed the `--stdin` flag. This is primarily useful when you have a large amount of paths which
  exceed the OS argument limit.

* The `nix-hash` command now supports Base64 and SRI. Use the flags `--base64`
  or `--sri` to specify the format of output hash as Base64 or SRI, and `--to-base64`
  or `--to-sri` to convert a hash to Base64 or SRI format, respectively.

  As the choice of hash formats is no longer binary, the `--base16` flag is also added
  to explicitly specify the Base16 format, which is still the default.

* The special handling of an [installable](../command-ref/new-cli/nix.md#installables) with `.drv` suffix being interpreted as all of the given [store derivation](@docroot@/glossary.md#gloss-store-derivation)'s output paths is removed, and instead taken as the literal store path that it represents.

  The new `^` syntax for store paths introduced in Nix 2.13 allows explicitly referencing output paths of a derivation.
  Using this is better and more clear than relying on the now-removed `.drv` special handling.

  For example,
  ```shell-session
  $ nix path-info /nix/store/gzaflydcr6sb3567hap9q6srzx8ggdgg-glibc-2.33-78.drv
  ```

  now gives info about the derivation itself, while

  ```shell-session
  $ nix path-info /nix/store/gzaflydcr6sb3567hap9q6srzx8ggdgg-glibc-2.33-78.drv^*
  ```
  provides information about each of its outputs.

* The experimental command `nix describe-stores` has been removed.

* Nix stores and their settings are now documented in [`nix help-stores`](@docroot@/command-ref/new-cli/nix3-help-stores.md).

* Documentation for operations of `nix-store` and `nix-env` are now available on separate pages of the manual.
  They include all common options that can be specified and common environment variables that affect these commands.

  These pages can be viewed offline with `man` using

  * `man nix-store-<operation>` and `man nix-env-<operation>`
  * `nix-store --help --<operation>` and `nix-env --help --<operation>`.

* Nix when used as a client now checks whether the store (the server) trusts the client.
  (The store always had to check whether it trusts the client, but now the client is informed of the store's decision.)
  This is useful for scripting interactions with (non-legacy-ssh) remote Nix stores.

  `nix store ping` and `nix doctor` now display this information.

* The new command `nix derivation add` allows adding derivations to the store without involving the Nix language.
  It exists to round out our collection of basic utility/plumbing commands, and allow for a low barrier-to-entry way of experimenting with alternative front-ends to the Nix Store.
  It uses the same JSON layout as `nix derivation show`, and is its inverse.

* `nix show-derivation` has been renamed to `nix derivation show`.
  This matches `nix derivation add`, and avoids bloating the top-level namespace.
  The old name is still kept as an alias for compatibility, however.

* The `nix derivation {add,show}` JSON format now includes the derivation name as a top-level field.
  This is useful in general, but especially necessary for the `add` direction, as otherwise we would need to pass in the name out of band for certain cases.
