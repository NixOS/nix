---
synopsis: Better error reporting for `with` expressions
prs: 9658
---

`with` expressions using non-attrset values to resolve variables are now reported with proper positions.

Previously an incorrect `with` expression would report no position at all, making it hard to determine where the error originated:

```
nix-repl> with 1; a
error:
       … <borked>

         at «none»:0: (source not available)

       error: value is an integer while a set was expected
```

Now position information is preserved and reported as with most other errors:

```
nix-repl> with 1; a
error:
       … while evaluating the first subexpression of a with expression
         at «string»:1:1:
            1| with 1; a
             | ^

       error: expected a set but found an integer
```
