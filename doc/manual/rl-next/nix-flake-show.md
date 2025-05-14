---
synopsis: "nix flake show now skips over import-from-derivation"
issues: 4265
prs: [12583]
---

Previously, if a flake contained outputs relying on [import from derivation](@docroot@/language/import-from-derivation.md) during evaluation, `nix flake show` would fail to display the rest of the flake. The updated behavior skips such outputs, allowing the rest of the flake to be shown.

