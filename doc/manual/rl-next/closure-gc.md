---
synopsis: "`nix store gc` can now collect garbage whithin a closure"
issues: 7239
prs: 8417
---

`nix store gc` can now be called with an installable argument, in which case it
will only collect the dead paths that are part of the closure of its argument.
