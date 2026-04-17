---
synopsis: "New `lint-function-comparison` setting"
prs: []
issues: [3371, 15531]
---

A new [`lint-function-comparison`](@docroot@/command-ref/conf-file.md#conf-lint-function-comparison)
setting warns or errors when `==` or `!=` reaches function values during comparison.

Comparing functions produces unreliable results:
it returns `false` when comparison reaches function values, but the
[value identity optimization](@docroot@/language/operators.md#value-identity-optimization)
can short-circuit and return `true` for the same logical comparison.
This may depend on evaluation order, whether a shared binding is used, and whether equality is invoked on the function directly or on a structure containing it.
The result is effectively nondeterministic.

This lint only fires when comparison actually reaches function values.
Cases where equality short-circuits before that point are not detected.

```
$ nix-instantiate --lint-function-comparison warn --eval -E 'let f = x: x; in f == f'
warning: function comparison is unreliable; …
false
```
