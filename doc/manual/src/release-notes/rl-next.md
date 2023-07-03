# Release X.Y (202?-??-??)

- [`nix-channel`](../command-ref/nix-channel.md) now supports a `--list-generations` subcommand

- Nix now allows unprivileged/[`allowed-users`](../command-ref/conf-file.md#conf-allowed-users) to sign paths.
  Previously, only [`trusted-users`](../command-ref/conf-file.md#conf-trusted-users) users could sign paths.
