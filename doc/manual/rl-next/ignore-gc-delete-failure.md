---
synopsis: "New setting `ignore-gc-delete-failure` for local stores"
prs: [15054]
---

A new local store setting `ignore-gc-delete-failure` has been added.
When enabled, garbage collection will log warnings instead of failing when it cannot delete store paths.
This is useful when running Nix as an unprivileged user that may not have write access to all paths in the store.

This setting is experimental and requires `--extra-experimental-features local-overlay-store`.
