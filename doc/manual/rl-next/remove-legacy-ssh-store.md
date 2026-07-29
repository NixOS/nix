---
synopsis: The legacy SSH store (`ssh://`) and `nix-store --serve` have been removed
---

The "legacy" SSH store, accessed via `ssh://` store URLs and implemented on top of `nix-store --serve`, has been removed, along with `nix-store --serve` itself and the underlying "serve" protocol.

Use `ssh-ng://` store URLs instead, which access the remote store by running `nix-daemon --stdio` over SSH.
Attempting to use an `ssh://` store URL or `nix-store --serve` now fails with an error explaining this.

Consequences:

- Entries in the `builders` setting (and `nix.buildMachines` on NixOS) that used the `ssh://` protocol must be switched to `ssh-ng://`.
Bare host names (no scheme) in `builders`, which used to imply `ssh://`, are now an error; spell out the scheme explicitly.

- `nix-copy-closure` now uses the `nix-daemon --stdio` protocol under the hood, so the remote host must have a working `nix-daemon` on its `PATH` (true of any Nix installation from the last several years).

- Hydra installations old enough to still use `nix-store --serve` on build machines cannot use machines running this version of Nix as builders.
  Upgrade to the latest version of Hydra instead.
