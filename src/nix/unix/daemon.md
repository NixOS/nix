R""(

# Examples

* Run the daemon:

  ```console
  # nix daemon
  ```

* Run the daemon and listen on standard I/O instead of binding to a UNIX socket:

  ```console
  # nix daemon --stdio
  ```

* Run the daemon and force all connections to be trusted:

  ```console
  # nix daemon --force-trusted
  ```

* Run the daemon and force all connections to be untrusted:

  ```console
  # nix daemon --force-untrusted
  ```

* Run the daemon, listen on standard I/O, and force all connections to use Nix's default trust:

  ```console
  # nix daemon --stdio --default-trust
  ```

# Description

This command runs the Nix daemon, which is a required component in
multi-user Nix installations. It runs build tasks and other
operations on the Nix store on behalf of non-root users. Usually you
don't run the daemon directly; instead it's managed by a service
management framework such as `systemd` on Linux, or `launchctl` on Darwin.

Note that this daemon does not fork into the background.

# Socket path

Unless `--stdio` is used, the daemon listens on a Unix domain socket.
The socket path is determined in the same way as for the [Local Daemon Store](@docroot@/store/types/local-daemon-store.md):

1. The [`NIX_DAEMON_SOCKET_PATH`](@docroot@/command-ref/env-common.md#env-NIX_DAEMON_SOCKET_PATH) environment variable, if set.

2. Otherwise, `$NIX_STATE_DIR/daemon-socket/socket`.
   See [`NIX_STATE_DIR`](@docroot@/command-ref/env-common.md#env-NIX_STATE_DIR) for details about the defaults when that variable is not defined.

# Systemd socket activation

`nix daemon` supports systemd socket-based activation using the
`nix-daemon.socket` unit in the Nix distribution. It supports
listening on multiple addresses; for example, the following stanza in
`nix-daemon.socket` makes the daemon listen on two Unix domain
sockets:

```
[Socket]
ListenStream=/nix/var/nix/daemon-socket/socket
ListenStream=/nix/var/nix/daemon-socket/socket-2
```

)""
