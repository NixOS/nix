---
synopsis: "Visual clutter in `--debugger` is reduced"
prs: 9919
---

Before:
```
info: breakpoint reached


Starting REPL to allow you to inspect the current state of the evaluator.

Welcome to Nix 2.20.0pre20231222_dirty. Type :? for help.

nix-repl> :continue
error: uh oh


Starting REPL to allow you to inspect the current state of the evaluator.

Welcome to Nix 2.20.0pre20231222_dirty. Type :? for help.

nix-repl>
```

After:

```
info: breakpoint reached

Nix 2.20.0pre20231222_dirty debugger
Type :? for help.
nix-repl> :continue
error: uh oh

nix-repl>
```
