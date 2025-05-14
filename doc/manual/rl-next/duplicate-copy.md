---
synopsis: "No longer copy flakes that are in the nix store"
issues: 10435
prs: [12877, 13098]
---

Previously, we would duplicate entries like `path:/nix/store/*` back into the Nix store.
This was prominently visible for pinned system flake registry entries in NixOS, e.g., when running `nix run nixpkgs#hello`.
