---
synopsis: "Attribute set equality now compares in lexicographic order of attribute names"
prs: [15327]
---

Attribute set equality (`==`) now compares attributes in lexicographic order of attribute names, rather than the internal symbol table order. Comparing attribute sets with mismatching `attrNames` now never evaluates any values, whereas previously that was undefined and depended on the unspecified symbol table order.

Previously, the evaluation order of attribute values during comparison was non-deterministic, meaning expressions like the following could silently succeed or fail depending on evaluation history or unspecified order of evaluation.

For example, depending on the order in which attribute names `a` and `b` have been seen by the evaluator, the following code would either yield `false` or fail to evaluate.

```nix
{ a = throw "oops"; b = 2; } == { a = 2; b = 1; }
```


```
nix-repl> a = 1

nix-repl> b = 2

nix-repl> { a = throw "oops"; b = 2; } == { a = 2; b = 1; }
error:
       … while calling the 'throw' builtin
         at «string»:1:7:
            1| { a = throw "oops"; b = 2; } == { a = 2; b = 1; }
             |       ^

       error: oops
```

```
nix-repl> b = 2

nix-repl> a = 1

nix-repl> { a = throw "oops"; b = 2; } == { a = 2; b = 1; }
false
```

