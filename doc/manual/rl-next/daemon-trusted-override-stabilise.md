---
synopsis: |
  `daemon-trust-override`, `--process-ops` stabilisation
prs: [16659, 16637]
---

`daemon-trust-override` experimental feature has been stabilised.
This change was needed to facilitate the daemon worker spawning rework.

`--force-untrusted` now behaves correctly when the `--stdio` daemon worker's store is another remote store backed by another daemon.

`--force-untrusted`, `--force-trusted`, `--default-trust`, `--process-ops` flags accepted by `nix-daemon` and `nix daemon` are no longer experimental.

