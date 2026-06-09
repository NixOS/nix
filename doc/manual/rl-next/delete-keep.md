---
synopsis: "Fixed a bug where keep-outputs and keep-derivations can interfere with delete commands"
prs: [15776]
---

Setting `keep-derivations = true` and trying to delete a derivation with realised outputs would previously fail.
Same with `keep-outputs = true` and trying to delete an output that still has derivers.
These options no longer affect the deletion commands, and are now documented as such.
