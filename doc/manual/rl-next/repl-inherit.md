---
synopsis: "`nix repl` now supports `inherit` and multiple bindings"
prs: [15082]
---

The `nix repl` now supports `inherit` statements and multiple bindings per line:

```
nix-repl> a = { x = 1; y = 2; }
nix-repl> inherit (a) x y
nix-repl> x + y
3

nix-repl> p = 1; q = 2;
nix-repl> p + q
3

nix-repl> foo.bar.baz = 1;
nix-repl> foo.bar
{ baz = 1; }
```
