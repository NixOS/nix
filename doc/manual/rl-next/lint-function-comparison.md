---
synopsis: "New `lint-function-comparison` setting"
prs: []
issues: [3371, 15531]
---

A new [`lint-function-comparison`](@docroot@/command-ref/conf-file.md#conf-lint-function-comparison)
setting warns or errors when `==`, `!=`, or `builtins.elem` reaches function
values during comparison.

Comparing functions produces unreliable results: the
[value identity optimization](@docroot@/language/operators.md#value-identity-optimization)
can short-circuit and return `true` for the same logical comparison that would
otherwise return `false`, depending on evaluation order and internal sharing.

When the value identity optimization short-circuits, the lint recursively
inspects already-evaluated values in the tree to detect functions that
would otherwise be silently masked.

```
$ nix-instantiate --lint-function-comparison warn --eval -E 'let f = x: x; in f == f'
warning: function comparison is unreliable; …
false
```
