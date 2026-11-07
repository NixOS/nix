---
synopsis: "`nix profile install` now respects the fragment when `?dir=` is also present"
issues: [15900]
---

When a flake URL combined a `?dir=` argument with a `#fragment` (e.g. `github:org/repo?dir=ai#my-tool`), `nix profile` derived the install name from `dir=` and ignored the fragment, causing collisions when installing multiple attributes from the same `?dir=` flake. The fragment now takes precedence, since it names a specific attribute; `dir=` is still used as a fallback when the fragment is absent, empty, or `default`.

Relatedly, `nix profile add` now detects that a package is already present by comparing its source (flake reference, attribute path and priority) rather than its name. Previously, re-adding a package that was stored under a different name than the one derived at the time — for instance, a name derived from `dir=` by an older version of Nix, or a collision-suffixed name such as `ai-1` — added a duplicate entry to the profile.
