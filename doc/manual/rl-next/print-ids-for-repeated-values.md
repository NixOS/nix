---
synopsis: Print IDs when values are repeated to disambiguate which values are repeated
issues: [10447]
prs: [15024]
---

Printing values in the nix repl will now assign IDs to repeated values and print these IDs.
This allows identifying which value is repeated.
Previously you would have to know from context what the repeated value is.

For example:

```
nix-repl> a = { foo = "uwu"; }

nix-repl> b = { bar = "snacc"; }

nix-repl> c = { baz = "boo"; }

nix-repl> [ a b c a b ]
[
  { ... } /* 0 */
  { ... } /* 1 */
  { ... }
  «repeated@0»
  «repeated@1»
]
```

In the last output you can e.g. see the `repeated@0` referencing the first value in the array, which has a comment `/* 0 */` to mark that it has the ID 0.

For a more complex example:

```
nix-repl> :p { a = { b = 2; }; s = "string"; n = 1234; x = rec { y = { z = { inherit y; }; }; }; }
{
  a = { b = 2; };
  n = 1234;
  s = "string";
  x = {
    y = {
      z = {
        y = «repeated@0»;
      };
    } /* 0 */;
  };
}
```

The drawback of this is that we need to fully scan the value twice: once to assign the IDs of repeated values and then again to actually print it.
This is also a stark UX change: when printing very large values there will be a long period where no user feedback is provided, whereas previously you would see lots of streaming output.
