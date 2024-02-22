---
synopsis: Functions are printed with more detail
prs: 9606
issues: 7145
---

Functions and `builtins` are printed with more detail in `nix repl`, `nix
eval`, `builtins.trace`, and most other places values are printed.

Before:

```
$ nix repl nixpkgs
nix-repl> builtins.map
«primop»

nix-repl> builtins.map lib.id
«primop-app»

nix-repl> builtins.trace lib.id "my-value"
trace: <LAMBDA>
"my-value"

$ nix eval --file functions.nix
{ id = <LAMBDA>; primop = <PRIMOP>; primop-app = <PRIMOP-APP>; }
```

After:

```
$ nix repl nixpkgs
nix-repl> builtins.map
«primop map»

nix-repl> builtins.map lib.id
«partially applied primop map»

nix-repl> builtins.trace lib.id "my-value"
trace: «lambda id @ /nix/store/8rrzq23h2zq7sv5l2vhw44kls5w0f654-source/lib/trivial.nix:26:5»
"my-value"

$ nix eval --file functions.nix
{ id = «lambda id @ /Users/wiggles/nix/functions.nix:2:8»; primop = «primop map»; primop-app = «partially applied primop map»; }
```

This was actually released in Nix 2.20, but wasn't added to the release notes
so we're announcing it here. The historical release notes have been updated as well.

[type-error]: https://github.com/NixOS/nix/pull/9753
[coercion-error]: https://github.com/NixOS/nix/pull/9754
