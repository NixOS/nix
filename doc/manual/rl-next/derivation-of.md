---
synopsis: "Add `builtins.derivationOf`"
issues: [10120]
---

The new [`builtins.derivationOf`](@docroot@/language/builtins.md#builtins-derivationOf)
returns the derivation reference recorded in the string context of a derivation
output. It is roughly the inverse of `builtins.outputOf`, and does not query the
Nix store for deriver metadata.
