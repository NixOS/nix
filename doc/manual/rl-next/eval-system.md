---
synopsis: Add new `eval-system` setting
prs: 4093
---

Add a new `eval-system` option.
Unlike `system`, it just overrides the value of `builtins.currentSystem`.
This is more useful than overriding `system`, because you can build these derivations on remote builders which can work on the given system.
In contrast, `system` also effects scheduling which will cause Nix to build those derivations locally even if that doesn't make sense.

`eval-system` only takes effect if it is non-empty.
If empty (the default) `system` is used as before, so there is no breakage.
