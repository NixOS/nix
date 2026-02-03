R""(

# Examples

* Run the daemon:

  ```console
  # nix store roots-daemon
  ```

# Description

This command runs a daemon that serves garbage collector roots from a Unix domain socket.
It is not required in all Nix installations, but is useful when the main Nix daemon
is not running as root and therefore cannot find runtime roots by scanning `/proc`.

When the garbage collector runs with [`use-roots-daemon`](@docroot@/store/types/local-store.md#store-experimental-option-use-roots-daemon)
enabled, it connects to this daemon to discover additional roots that should not be collected.

The daemon listens on [`<state-dir>`](@docroot@/store/types/local-store.md#store-option-state)`/gc-roots-socket/socket` (typically `/nix/var/nix/gc-roots-socket/socket`).

# Protocol

The protocol is simple.
For each client-initiated Unix socket connection, the server:

1. Sends zero or more [store paths](@docroot@/store/store-path.md) as NUL-terminated (`\0`) strings.
2. Closes the connection.

Example (with `\0` shown as newlines for clarity):
```
/nix/store/s66mzxpvicwk07gjbjfw9izjfa797vsw-hello-2.12.1
/nix/store/fvpr7x8l3illdnziggvkhdpf6vikg65w-git-2.44.0
```

# Security

No information is provided as to which processes are opening which store paths.
While only the main Nix daemon needs to use this daemon, any user able to talk to the main Nix daemon can already obtain the same information with [`nix-store --gc --print-roots`](@docroot@/command-ref/nix-store/gc.md).

Therefore, restricting this daemon to only accept the Nix daemon as a client is, while recommended for defense-in-depth reasons, strictly speaking not reducing what information can be extracted versus merely restricting this daemon to accept connections from any [allowed user](@docroot@/command-ref/conf-file.md#conf-allowed-users).

# Systemd socket activation

`nix store roots-daemon` supports systemd socket-based activation, [just like `nix-daemon`](@docroot@/command-ref/nix-daemon.md#systemd-socket-activation).
)""
