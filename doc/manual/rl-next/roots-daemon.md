---
synopsis: New command `nix store roots-daemon` for serving GC roots
prs: [15143]
---

New command [`nix store roots-daemon`](@docroot@/command-ref/new-cli/nix3-store-roots-daemon.md) runs a daemon that serves garbage collector roots over a Unix domain socket.
It enables the garbage collector to discover runtime roots when the main Nix daemon doesn't have `CAP_SYS_PTRACE` capability and therefore cannot scan `/proc`.

The garbage collector can be configured to use this daemon via the [`use-roots-daemon`](@docroot@/store/types/local-store.md#store-experimental-option-use-roots-daemon) store setting.

This feature requires the [`local-overlay-store` experimental feature](@docroot@/development/experimental-features.md#xp-feature-local-overlay-store).
