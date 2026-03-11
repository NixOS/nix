R"(

**Store URL format**: `daemon`, `unix://`*path*

This store type accesses a Nix store by talking to a Nix daemon
listening on the Unix domain socket *path*.

When *path* is not specified (i.e. the `daemon` pseudo-URL or bare `unix://`),
the socket path is determined as follows:

1. The [`NIX_DAEMON_SOCKET_PATH`] environment variable, if set.

2. Otherwise, `$NIX_STATE_DIR/daemon-socket/socket`.
   See [`NIX_STATE_DIR`] for details about the defaults when that variable is not defined.

[`NIX_DAEMON_SOCKET_PATH`]: @docroot@/command-ref/env-common.md#env-NIX_DAEMON_SOCKET_PATH
[`NIX_STATE_DIR`]: @docroot@/command-ref/env-common.md#env-NIX_STATE_DIR

)"
