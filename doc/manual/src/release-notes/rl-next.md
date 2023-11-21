# Release X.Y (202?-??-??)

- Fixed a bug where `nix-env --query` ignored `--drv-path` when `--json` was set.

- Introduce experimental mounted/unmounted variations to our remote stores:

  - [`mounted-ssh-ng://`](@docroot@/command-ref/new-cli/nix3-help-stores.md)

    This store allows full access to a Nix store on a remote machine and additionally requires that the store be mounted in the local filesystem.

  - [`unmounted-unix://`](@docroot@/command-ref/new-cli/nix3-help-stores.md)

    This store communicates over a Unix domain socket like the regular `unix:://` store, but without assuming that the store is available mounted on the local file system.

  These are enabled under the
  [`remote-store-mount-variations`](@docroot@/contributing/experimental-features.md#xp-remote-store-mount-variations)
  experimental feature.

  Note that the syntax is likely to change:
  having double the schemes is much worse than `?mounted={true,false}` on the existing schemes.
