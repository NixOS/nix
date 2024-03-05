---
synopsis: Nested debuggers are no longer supported
prs: 9920
---

Previously, evaluating an expression that throws an error in the debugger would
enter a second, nested debugger:

```
nix-repl> builtins.throw "what"
error: what


Starting REPL to allow you to inspect the current state of the evaluator.

Welcome to Nix 2.18.1. Type :? for help.

nix-repl>
```

Now, it just prints the error message like `nix repl`:

```
nix-repl> builtins.throw "what"
error:
       … while calling the 'throw' builtin
         at «string»:1:1:
            1| builtins.throw "what"
             | ^

       error: what
```
