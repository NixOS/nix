---
synopsis: Cycle detection in `nix repl` is simpler and more reliable
prs: 9926
issues: 8672
---

The cycle detection in `nix repl`, `nix eval`, `builtins.trace`, and everywhere
else values are printed is now simpler and matches the cycle detection in
`nix-instantiate --eval` output.

Before:

```
nix eval --expr 'let self = { inherit self; }; in self'
{ self = { self = «repeated»; }; }
```

After:

```
{ self = «repeated»; }
```
