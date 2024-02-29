---
synopsis: Concise error printing in `nix repl`
prs: 9928
---

Previously, if an element of a list or attribute set threw an error while
evaluating, `nix repl` would print the entire error (including source location
information) inline. This output was clumsy and difficult to parse:

```
nix-repl> { err = builtins.throw "uh oh!"; }
{ err = «error:
       … while calling the 'throw' builtin
         at «string»:1:9:
            1| { err = builtins.throw "uh oh!"; }
             |         ^

       error: uh oh!»; }
```

Now, only the error message is displayed, making the output much more readable.
```
nix-repl> { err = builtins.throw "uh oh!"; }
{ err = «error: uh oh!»; }
```

However, if the whole expression being evaluated throws an error, source
locations and (if applicable) a stack trace are printed, just like you'd expect:

```
nix-repl> builtins.throw "uh oh!"
error:
       … while calling the 'throw' builtin
         at «string»:1:1:
            1| builtins.throw "uh oh!"
             | ^

       error: uh oh!
```

