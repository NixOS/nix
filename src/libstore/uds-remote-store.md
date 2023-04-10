R"(

**Store URL format**: `daemon`, `unix://`*path*

This store type accesses a Nix store by talking to a Nix daemon
listening on the Unix domain socket *path*. The store pseudo-URL
`daemon` is equivalent to `unix:///nix/var/nix/daemon-socket/socket`.

)"
