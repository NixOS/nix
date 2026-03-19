R"(

**Store URL format**: `daemon`, `unix://`*path*

This store type accesses a Nix store by talking to a Nix daemon
listening on the Unix domain socket *path*.

When *path* is not specified (i.e. the `daemon` pseudo-URL or bare `unix://`),
the socket path is determined as follows:

1. The [`NIX_DAEMON_SOCKET_PATH`] environment variable, if set.

2. Otherwise, `daemon-socket/socket` within the [`state`](#store-setting-state) directory.

[`NIX_DAEMON_SOCKET_PATH`]: @docroot@/command-ref/env-common.md#env-NIX_DAEMON_SOCKET_PATH

)"
