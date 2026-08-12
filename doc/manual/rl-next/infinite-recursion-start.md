---
synopsis: Start of infinite recursion is now visible
issues: []
prs: [16296]
---

When Nix reports a trace for an infinitely recursive evaluation,
it will now let you discern the cycle from the code that leads up to the cycle.

Example:

`nix eval --expr 'let f = builtins.add f 1; in toString f' --show-trace`
```
error:
       … while calling the 'toString' builtin
         at «string»:1:30:
            1| let f = builtins.add f 1; in toString f
             |                              ^

       … entering the infinite recursion

       … while calling the 'add' builtin
         at «string»:1:9:
            1| let f = builtins.add f 1; in toString f
             |         ^

       error: infinite recursion encountered
```