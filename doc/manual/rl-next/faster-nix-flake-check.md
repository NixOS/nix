---
synopsis: "`nix flake check` now skips derivations that can be substituted"
prs: [13574]
---

Previously, `nix flake check` would evaluate and build/substitute all
derivations. Now, it will skip downloading derivations that can be substituted.
This can drastically decrease the time invocations take in environments where
checks may already be cached (like in CI).
