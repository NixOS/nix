---
synopsis: Show all FOD errors with `nix build --keep-going`
---

`nix build --keep-going` now behaves consistently with `nix-build --keep-going`. This means
that if e.g. multiple FODs fail to build, all hash mismatches are displayed.
