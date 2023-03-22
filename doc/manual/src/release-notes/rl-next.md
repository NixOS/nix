# Release X.Y (202?-??-??)

* Commands which take installables on the command line can now read them from the standard input if
  passed the `--stdin` flag. This is primarily useful when you have a large amount of paths which
  exceed the OS arg limit.

* The `nix-hash` command now supports Base64 and SRI. Use the flags `--base64`
  or `--sri` to specify the format of output hash as Base64 or SRI, and `--to-base64`
  or `--to-sri` to convert a hash to Base64 or SRI format, respectively.

  As the choice of hash formats is no longer binary, the `--base16` flag is also added
  to explicitly specify the Base16 format, which is still the default.

* The special handling of an [installable](../command-ref/new-cli/nix.md#installables) with `.drv` suffix being interpreted as all of the given [store derivation](../glossary.md#gloss-store-derivation)'s output paths is removed, and instead taken as the literal store path that it represents.

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