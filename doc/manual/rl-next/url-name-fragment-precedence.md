---
synopsis: "`nix profile install` now respects the fragment when `?dir=` is also present"
issues: [15900]
---

When a flake URL combined a `?dir=` argument with a `#fragment` (e.g. `github:org/repo?dir=ai#my-tool`), `nix profile` derived the install name from `dir=` and ignored the fragment, causing collisions when installing multiple attributes from the same `?dir=` flake. The fragment now takes precedence, since it names a specific attribute; `dir=` is still used as a fallback when the fragment is absent, empty, or `default`.
