---
synopsis: "Store GCs can now be restricted with a minimum age"
prs: [14725]
issues: [7572]
---

Nix store GCs invoked using either `nix-collect-garbage` or `nix store gc` may
now be restricted to only deleting paths older than a certain minimum age, i.e.
whose most recent usage is more than *n* days in the past. "Usage" is
intentionally defined ambiguously, however in general all operations which
produce/require the presence of a given store path count as "usage".


Example usage:

```bash
# Delete store paths older than 7 days
nix store gc --older-than 7d

# Alternatively:
nix-collect-garbage --path-older-than 7d
```
