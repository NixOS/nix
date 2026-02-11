---
synopsis: "Improve daemon socket path logic for chroot stores"
prs: [15429]
---

The default daemon socket path now uses the per-store [`state`](@docroot@/store/types/local-store.md#store-setting-state) directory whenever one is defined, rather than always using the global [`NIX_STATE_DIR`](@docroot@/command-ref/env-common.md#env-NIX_STATE_DIR).
This means [local chroot stores](@docroot@/store/types/local-store.md#chroot) each get their own socket path automatically.

Example:

```bash
nix-daemon --store /foo/bar
```

will now use a socket at:
```
/foo/bar/nix/var/nix/daemon-socket/socket
```
instead of
```
$NIX_STATE_DIR/daemon-socket/socket
```

Users who wish to serve or connect to a chroot store at the old location will have to force the socket location:

- When serving (running a daemon), use the new [`--socket-path`](@docroot@/command-ref/new-cli/nix3-daemon.md#opt-socket-path) flag:

  ```bash
  nix daemon --socket-path "$NIX_STATE_DIR/daemon-socket/socket"
  ```

- When connecting as a client  put the path in the [store URL](@docroot@/store/types/local-daemon-store.md):

  ```
  unix://$NIX_STATE_DIR/daemon-socket/socket
  ```
