# Release X.Y (202?-??-??)

- [`nix-channel`](../command-ref/nix-channel.md) now supports a `--list-generations` subcommand

- The function [`builtins.fetchClosure`](../language/builtins.md#builtins-fetchClosure) can now fetch input-addressed paths in [pure mode](../command-ref/conf-file.md#conf-pure-eval).

- Nix now allows unprivileged/[`allowed-users`](../command-ref/conf-file.md#conf-allowed-users) to sign paths.
  Previously, only [`trusted-users`](../command-ref/conf-file.md#conf-trusted-users) users could sign paths.
